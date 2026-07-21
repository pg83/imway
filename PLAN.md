## Вердикт

У imway уже хорошая основа современного HDR-композитора:

```text
surface electrical values
  → decode transfer function
  → conversion to linear BT.2020, absolute nits
  → FP16 composition
  → tone/gamut/output mapping
  → sRGB or BT.2020/PQ
  → XRGB8888/XRGB2101010
  → KMS
```

Главное архитектурное решение правильное: единая линейная FP16-сцена в абсолютных нитах, а не отдельные случайно склеенные SDR/HDR-пути. Поверхности, UI, blur и output transform в основном живут в согласованной модели.

Но полноценным, физически достоверным color-managed pipeline это пока назвать нельзя. Рабочий режим сейчас — один известный монитор, обычные sRGB/PQ-клиенты и заранее выставленная яркость. За пределами этого режима есть реальные ошибки, расхождения с Wayland-протоколами и места, где мы декларируем больше, чем исполняем.

Самые серьёзные проблемы:

1. `set_luminances` рекламируется, но reference white для sRGB и HLG фактически игнорируется.
2. Обычный SDR white сжимается tone mapper’ом и не достигает белого выхода.
3. «HDR holds hardware brightness at calibration point» — неправда: imway яркость туда не выставляет.
4. Dither выбирается по формату framebuffer, а не по фактической глубине линка.
5. Hardware cursor обходит color management поверхности.
6. Direct scanout небезопасен для ARGB/полупрозрачных поверхностей.
7. Ошибка acquire fence не исключает поверхность из отрисовки, несмотря на комментарий.
8. Произвольные primaries принимаются без проверки и могут породить NaN/Inf.
9. Заявленный `perceptual` intent по существу не реализован.
10. Реальные KMS/HDR режимы тестами почти не покрыты.

## Что сделано хорошо

FP16 scene target создаётся как `VK_FORMAT_R16G16B16A16_SFLOAT` в [renderer.cpp](/home/pg/monorepo/imway/renderer.cpp:1263). Этого достаточно для 10 000 нит, отрицательных extended-linear значений и линейного blending без промежуточной 8/10-битной квантизации.

В [imgui_scene.frag](/home/pg/monorepo/imway/imgui_scene.frag:121) действительно реализованы:

- sRGB;
- PQ;
- HLG;
- extended linear/scRGB;
- BT.1886;
- gamma 2.2;
- ICC gamma;
- sRGB, Display P3, BT.2020 и custom primaries;
- electrical, optical-premultiplied и straight alpha;
- NV12/P010, full/limited range, разные chroma locations.

UI, legacy-контент и фон переводятся в линейный BT.2020 и получают SDR white в нитах. Lockscreen blur работает над линейной FP16-сценой, что правильно и для HDR.

В [output_transform.frag](/home/pg/monorepo/imway/output_transform.frag:46) последовательно выполняются tone mapping, gamut compression, PQ/sRGB encoding и dithering.

HDR KMS path тоже логически цельный:

- XR30;
- `BT2020_RGB`;
- `HDR_OUTPUT_METADATA`;
- PQ уже сформирован shader’ом;
- старые CRTC DEGAMMA/CTM/GAMMA сбрасываются в passthrough.

Это согласовано с реализацией в [device_kms.cpp](/home/pg/monorepo/imway/device_kms.cpp:1509).

Интерактивный screenshot path для KMS удачный: готовый scanout dma-buf передаётся viewer’у во владение, compositor переключается на replacement buffer. HDR JXL кодируется lossless, 16-bit, BT.2100/PQ.

## Ошибки высокой важности

### 1. `set_luminances` поддержан только частично

Color-management protocol требует anchoring: reference luminance разных image descriptions должен приводить к одному физическому уровню. Это прямо сформулировано в [спецификации color-management](https://wayland.app/protocols/color-management-v1).

imway рекламирует `SET_LUMINANCES` в [wayland.cpp](/home/pg/monorepo/imway/wayland.cpp:8312), сохраняет значения, но renderer передаёт `referenceNits` только для extended-linear, gamma22 и ICC-gamma. Для sRGB передаётся ноль, после чего shader подставляет глобальный `sdrWhiteNits`:

- явно tagged sRGB с reference white 80, 100 или 203 нит будет выглядеть одинаково;
- Display P3 с sRGB transfer имеет ту же проблему;
- ICC с точной sRGB TRC использует глобальный white, а практически такой же ICC с pure-gamma TRC — профильный reference white;
- HLG всегда рассчитывается относительно жёстко заданных 1000 нит и игнорирует переопределённые luminances/reference.

То есть протокол принимает описание, возвращает `ready`, но не исполняет часть его семантики.

### 2. SDR white проходит через собственный highlight shoulder

В SDR compositor помещает обычный белый в сцену как 203 нита:

- [renderer.cpp](/home/pg/monorepo/imway/renderer.cpp:4094) — UI и legacy SDR white = 203;
- [color.cpp](/home/pg/monorepo/imway/color.cpp:386) — SDR output peak = 203;
- tone-map knee = 90% peak.

Поэтому входные 203 нита превращаются примерно в 192.85 нита:

```text
white linear output ≈ 192.85 / 203 = 0.95
encoded sRGB ≈ 0.978
```

Белый compositor’а получается около 249/255 вместо 255/255.

Последствия:

- обычный SDR white никогда не доходит до полного белого;
- при переключении fullscreen-приложения между composition и direct scanout яркость может подпрыгивать: direct scanout отдаёт code white 1.0, composition — около 0.978;
- HDR slider также врёт около верхней границы: если SDR white выставлен близко к display peak, tone mapper снова его уменьшит.

Поле `OutputMapping::referenceNits` вычисляется, но output shader его вообще не использует.

### 3. Физическая HDR-калибровка не обеспечена

При HDR запуске печатается:

> HDR holds hardware brightness at its calibration point

Но код только прячет physical-brightness control и перенаправляет клавиши на SDR white. Он не выставляет sysfs/DDC backlight в максимум или в известную calibration position.

Следовательно, заявленные:

- абсолютные ниты;
- `--hdr-peak`;
- SDR white;
- MaxCLL/MaxFALL;
- Wayland output image description

могут не иметь отношения к реальной яркости панели.

Если монитор сейчас стоит на 35% DDC brightness, imway всё равно считает, что 600 нит в shader — это физические 600 нит. Это крупнейшее расхождение между математическим и реальным pipeline.

### 4. Framebuffer bpc смешан с link bpc

Shader выбирает амплитуду dithering исключительно по `VK_FORMAT_A2R10G10B10_UNORM_PACK32` или 8-bit format в [renderer.cpp](/home/pg/monorepo/imway/renderer.cpp:2056).

Но SDR по умолчанию:

- имеет `color.bpc = 8`;
- может получить XR30 framebuffer;
- connector `max bpc` при этом запрашивается как 8.

То есть compositor гордо пишет `10-bit scanout`, dithering выполняется под 10 бит, но физический link может быть 8-битным. Последующее hardware truncation уже не получает корректный 8-bit dither.

Обратные комбинации тоже не моделируются:

- `--bpc 12` всё равно имеет только 10-bit XR30 framebuffer;
- если link динамически деградирует с 10 до 8 bpc, shader продолжает dither под framebuffer format;
- без `link bpc` feedback HDR остаётся включённым с непроверенной глубиной линка.

Нужно разделить как минимум:

```text
scene precision
framebuffer precision
requested link bpc
actual link bpc
dither target precision
```

### 5. Managed hardware cursor обходит pipeline

Для client cursor hardware path проверяет только:

- SHM;
- размер;
- отсутствие scale/transform/viewport.

Затем ARGB байты копируются напрямую в cursor plane в [renderer.cpp](/home/pg/monorepo/imway/renderer.cpp:3732).

Не проверяются:

- color description;
- primaries;
- transfer function;
- straight/optical alpha.

Следовательно, SDR managed cursor может иметь неправильный цвет и alpha. В HDR hardware cursor отключён, поэтому там путь корректнее.

### 6. Direct scanout принимает потенциально прозрачный ARGB

Direct scanout исключает non-default alpha representation, но default `premultiplied_electrical` не означает opaque. ARGB8888/ARGB2101010 поверхность с alpha < 1 может пройти.

`wl_surface.set_opaque_region` при этом вообще пустой обработчик в [wayland.cpp](/home/pg/monorepo/imway/wayland.cpp:1102). Поэтому compositor не может доказать opaque coverage.

Результат зависит от поведения primary plane: alpha может игнорироваться или смешиваться не так, как при composition. Безопасный путь — принимать только XRGB/XBGR либо реально отслеживать opaque region и свойства plane blending.

### 7. Acquire-fence failure не пропускает поверхность

В [renderer.cpp](/home/pg/monorepo/imway/renderer.cpp:3905) комментарий говорит «skip just this surface». Но `return` выходит только из visitor lambda. Затем вызывается `buildUi()`, и поверхность рисуется обычным путём.

При неудаче:

- timeline transfer;
- sync-file export;
- Vulkan semaphore import

поверхность всё равно может быть sampled без acquire wait. Это correctness bug explicit sync.

Для implicit sync ошибка `DMA_BUF_IOCTL_EXPORT_SYNC_FILE` тоже просто игнорируется, после чего buffer sampled без явного fallback.

### 8. Custom primaries могут отравить весь shader math

`set_primaries` принимает любые координаты и сразу создаёт ready description в [wayland.cpp](/home/pg/monorepo/imway/wayland.cpp:7759).

В `rgbToXyz()` есть деления на `yr`, `yg`, `yb`, `yw`. Нули, вырожденный triangle или невалидный white point дают Inf/NaN. Проверки конечности матрицы нет.

Такой image description должен завершаться `failed(unsupported)`, а не становиться ready.

### 9. `perceptual` intent заявлен, но не реализован

imway рекламирует только обязательный perceptual intent. После проверки enum intent забывается.

Фактически применяется одна фиксированная схема:

- luma-preserving shoulder;
- нейтральное chroma compression;
- одна chromatic adaptation.

ICC parser ещё прямее использует `INTENT_RELATIVE_COLORIMETRIC` в [icc.cpp](/home/pg/monorepo/imway/icc.cpp:114).

Таким образом, особенно для ICC, заявленное «perceptual» и выполненное преобразование различаются.

## HDR metadata

PQ/HLG распознаются как HDR, всё остальное — как SDR. Это ломает extended-linear/scRGB:

- scRGB способен содержать значения до 10 000 нит;
- `ColorDescription::hdr()` возвращает false;
- metadata aggregator записывает для такой поверхности всего лишь глобальный SDR white.

То же упрощение применяется ко всему non-PQ/HLG контенту, игнорируя его `maxNits`.

Кроме того:

- metadata агрегируется по описаниям всех mapped surfaces, а не по реально видимым/неперекрытым пикселям;
- MaxFALL — максимум source declarations, не реальный frame average;
- неизвестный PQ MaxCLL консервативно считается 10 000 нит, что безопасно, но грубо.

Основной HDR10 metadata path рабочий, но scRGB и mixed-content metadata неверны.

## Типы поверхностей

| Поверхность | Что работает | Проблемы |
|---|---|---|
| SHM ARGB/XRGB8888 | sRGB и tagged color descriptions, линейный blending | Только 8 бит; scaling до EOTF; managed hardware cursor bypass |
| Single-pixel buffer | Корректно попадает в общий pipeline | Только обычный RGB |
| RGB dma-buf 8/10-bit | Zero-copy import, color descriptions | Direct scanout только узкий SDR subset |
| FP16 dma-buf | extended-linear/scRGB | HDR metadata считает его SDR |
| NV12/P010 | BT.601/709/2020, full/limited, chroma location | Только 4:2:0 и два формата; простой chroma filter |
| Subsurface/popup/drag icon | Наследует общий surface shader | Проблемы фильтрации и sync те же |
| Client cursor | Software path корректный | Hardware path игнорирует color/alpha description |
| ImGui/UI | Линейный SDR в FP16 | SDR white сжимается tone mapper’ом |
| Lockscreen blur | Линейный FP16 blur | Хороший путь |
| Direct scanout | SDR untagged fullscreen dmabuf | Нет HDR; потенциально прозрачный ARGB; нет explicit sync |
| Screenshot | KMS dma-buf handoff, HDR JXL | Fallback теряет precision; legacy PPM не color-managed |

## Масштабирование и качество изображения

Все client textures используют один линейный Vulkan sampler. Sampling происходит до EOTF.

Значит, интерполяция выполняется в encoded domain:

- PQ/HLG highlights масштабируются фотометрически неверно;
- HDR gradients меняют luminance;
- straight-alpha края могут давать цветное bleeding;
- electrical-premultiplied края интерполируются до unpremultiply/EOTF;
- YUV chroma использует простой bilinear reconstruction.

Для пиксель-то-пиксель отображения всё нормально. Для scale/viewport/resize это заметный quality gap. Правильный high-quality путь требует декодирования/линеаризации до resampling либо специализированных reconstruction shaders.

Dithering статический и screen-space. Он не мерцает, но может показывать структуру; temporal/blue-noise dithering отсутствует.

Tone/gamut mapper очень простой. Он монотонный и безопасно ограничивает gamut, но это не SOTA perceptual mapping: нет hue-preserving compression, adaptive knee, content-aware mapping или 3D LUT.

## SDR output

SDR output всегда трактуется как sRGB с виртуальным peak 203. EDID primaries, white point и реальная яркость панели игнорируются.

При этом Wayland output image description сообщает стандартные sRGB 0.2/80 nit параметры. Реальный дисплей не измерен, backlight не учтён, shader работает в условной 203-nit нормализации.

То есть SDR color management сейчас фактически означает:

> преобразовать всё в стандартный sRGB signal

а не:

> воспроизвести заданную колориметрию на конкретном SDR-мониторе.

Нужны output ICC/calibration profile или хотя бы отдельная явная модель SDR display primaries/white/peak.

## HDR output и настройки

`--hdr N` включает HDR только при запуске. Runtime slider меняет SDR white, но не включает/выключает HDR mode.

`--hdr-min`, `--hdr-peak`, `--hdr-fall` перекрывают EDID. Это полезно, но значения полностью доверяются пользователю и никак не связаны с backlight calibration.

`--bpc` означает скорее connector max-bpc request, а не реальную precision всего pipeline.

`--rgb-range` реализован через connector `Broadcast RGB`, что правильно. Shader остаётся full-range; hardware выполняет signal-range conversion.

Night light применяется в линейном output transform и отключает direct scanout — это хороший выбор. Но аппаратного offload нет.

Параметр SDR white влияет на legacy/UI sRGB, но не одинаково на все transfer functions. Поэтому «яркость SDR в HDR» пока не является универсальной настройкой всего SDR-контента.

## KMS robustness

Перед первым modeset выполняется `TEST_ONLY`, это хорошо. Но если HDR color/link configuration отвергнута, автоматического перехода на SDR нет: present возвращает false, и успешного modeset может не случиться.

Если `setupHdr()` падает раньше, `color` сбрасывается в дефолтный SDR, но некоторые уже рассчитанные KMS values (`maxBpcValue`) остаются от HDR-конфигурации. Внутренняя модель и запрос connector’у могут разойтись.

Современный DRM Color Pipeline API не используется. Сейчас весь transform выполняется shader’ом, а legacy LUT/CTM сбрасываются. Новый API позволяет обнаруживать цепочки hardware color operations вместо hardcode и является правильным SOTA-направлением; он всё ещё отмечен в PLAN. [Документация Linux DRM Color Pipeline](https://docs.kernel.org/gpu/rfc/color_pipeline.html).

HDR direct scanout также отсутствует — сознательно и правильно, пока нельзя доказать совпадение encoding, metadata, range, bpc и synchronization.

## Протоколы

### Color management v3

Реализован хороший, но неполный subset:

- parametric descriptions;
- ICC v2/v4;
- sRGB, P3, BT.2020/custom;
- sRGB/PQ/HLG/scRGB/BT.1886/gamma22;
- Windows scRGB/BT2100;
- output/preferred descriptions.

Проблемы:

- `SET_LUMINANCES` исполнен не полностью;
- perceptual intent фиктивен;
- ICC поддерживает только RGB matrix-shaper с sRGB/linear/pure-gamma TRC;
- LUT ICC, сложные parametric curves, BPC и полноценный ICC transform отсутствуют;
- foreign image-description reference всегда получает graceful `unsupported`;
- arbitrary primaries не валидируются;
- manager `get_output` игнорирует переданный `wl_output`, что пока незаметно из-за единственного output.

Отказ от неподдерживаемого ICC-профиля разрешён протоколом; это честный narrow subset, а не protocol violation.

### Color representation v1

Поддерживаются три alpha mode и наиболее полезные BT.601/709/2020 full/limited варианты. Математика alpha в обычном composition path выглядит правильной. [Спецификация color-representation](https://wayland.app/protocols/color-representation-v1).

Не поддерживаются и не рекламируются:

- FCC;
- SMPTE 240;
- BT.2020 constant luminance;
- ICtCp;
- другие subsampling formats.

Это честное ограничение.

### linux-dmabuf

Рекламируется максимум v4 при наличии feedback, хотя установленная спецификация уже v6. Поэтому нет sampling tranche flag v6. Feedback содержит одну tranche со всеми formats и flags=0; scanout tranche отсутствует, хотя SDR direct scanout существует. Клиенты не могут выбрать scanout-friendly allocation. [Спецификация linux-dmabuf](https://wayland.app/protocols/linux-dmabuf-v1).

Bounds validation неполная: проверяется `offset + stride * full_height` для каждой plane, но нет полноценной format-aware проверки минимального stride, размера последней строки и chroma plane geometry. Vulkan import часто отсеет плохой buffer, но протокольная проверка слабая.

### Presentation time

Рекламируется только v1.

Не отправляется `sync_output`, хотя output у compositor один и его легко указать. При отсутствии kernel pageflip timestamp используется `CLOCK_MONOTONIC now`, но всё равно ставится `VSYNC`. Спецификация прямо говорит, что software scheduling недостаточен для этого флага. [Спецификация presentation-time](https://wayland.app/protocols/presentation-time).

Direct scanout не получает `ZERO_COPY`. VRR semantics версии 2 не реализованы.

### Fractional scale

Протокол формально есть, но `preferred_scale` всегда 120. `--scale 1.5` масштабирует compositor UI, а client’у продолжает сообщаться 1.0. Это зафиксированный XFAIL и реальная незавершённая функция.

### Не поддержаны современные rendering/presentation protocols

- `wp_tearing_control_v1`;
- `wp_content_type_v1`;
- `wp_fifo_v1`;
- `wp_commit_timing_v1`;
- `wp_alpha_modifier_v1`;
- стандартный image-copy-capture/image-capture-source path для screenshots.

Commit timing позволяет клиенту задавать target presentation time, alpha modifier — compositor-side opacity, остальные нужны для video/games/frame pacing. Например, [commit-timing](https://wayland.app/protocols/commit-timing-v1) связывает commit с presentation clock, а [alpha-modifier](https://wayland.app/protocols/alpha-modifier-v1) задаёт alpha multiplier в blending space.

## Screenshots и color picker

KMS handoff path — сильная часть реализации.

HDR JXL:

- 16-bit;
- lossless;
- BT.2100 primaries;
- PQ;
- intensity target/min nits.

Но fallback path преобразует XR30 в 8-bit RGBA, сохраняя HDR label. На headless/dumb fallback это даст banding и потерю precision.

Старый `--screenshot PATH` пишет PPM:

- всего 8 бит;
- в HDR это усечённые PQ code values;
- профиля и HDR metadata нет.

Это диагностический dump, а не переносимый screenshot.

Color picker также читает готовый scanout code. В HDR он возвращает hex от PQ-encoded пикселя, а не sRGB, не linear BT.2020 и не ниты. Пользовательский смысл такого `#rrggbb` вводит в заблуждение.

## Тесты

Я собрал проект и прогнал весь suite через актуальный `set/pg/libs`:

```text
293 сценария
879 запусков
287 ok
0 flaky
0 fail
5 skip
1 xfail
```

XFAIL — fractional HiDPI с hardcoded 120.

Покрытие действительно сильное для headless behavioral testing: есть PQ, HLG, scRGB, BT.1886, gamma22, P3, custom primaries, ICC subset, alpha modes, YUV, dithering, metadata, HDR→SDR, night light, screenshot/JXL и direct-scanout policy.

Но suite не видит важнейший класс ошибок:

- реальный KMS signal и HDMI/DP InfoFrames;
- фактический link bpc/range;
- backlight calibration;
- HDR atomic rejection и SDR fallback;
- tagged sRGB с разными reference luminances;
- HLG с custom luminances;
- degenerate custom primaries;
- managed hardware cursor;
- transparent ARGB direct scanout;
- acquire-fence export/import failure;
- SDR white около output peak;
- scRGB MaxCLL metadata;
- scaled PQ/HLG и alpha gradients;
- LUT ICC;
- mixed HDR + SDR + YUV + translucent UI + night light;
- screenshot fallback precision.

Поэтому зелёные 879 запусков подтверждают стабильность известного пути, но не физическую и протокольную полноту color pipeline.

## Рекомендуемый порядок работ

1. Исправить reference-luminance semantics и добавить таблицу тестов transfer × reference × HDR/SDR output.
2. Сделать tone mapping reference-aware: SDR white и значения до reference white не должны двигаться.
3. Валидировать custom primaries до создания ready description.
4. Реально исключать surface из frame при неготовом acquire point.
5. Запретить managed cursor hardware path и прозрачный direct scanout.
6. Разделить framebuffer/link/dither bpc и тестировать фактический link feedback.
7. Исправить metadata для extended-linear и других managed SDR descriptions.
8. Заменить ложное «brightness held at calibration point» на реальное управление либо явное требование внешней калибровки.
9. Добавить SDR fallback после rejected HDR atomic test.
10. Довести presentation feedback и fractional scale.
11. Добавить per-output SDR profile/calibration и затем DRM Color Pipeline offload.
12. После этого делать безопасный HDR direct scanout и современные scheduling protocols.

Итоговая оценка: архитектурно pipeline уже хороший и заметно выше уровня «HDR checkbox». Но его математическое ядро сейчас сильнее, чем протокольные края и физический output contract. Основная следующая работа — не ещё один transfer function, а согласование reference white, реальной панели, bpc/sync/direct-scanout и того, что мы сообщаем клиентам.

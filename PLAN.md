## Вердикт

У imway уже не «заготовка HDR», а хорошая базовая архитектура:

```text
client image
  → decode transfer function
  → linear BT.2020, абсолютные nits, FP16
  → композиция поверхностей и UI
  → output transform
  → PQ/BT.2020/XR30
  → KMS
```

Это правильнее большинства небольших композиторов. Непрозрачный HDR10/PQ happy path работает хорошо. Но до полноценного production-grade color management ещё далеко: неполная реализация Wayland-протокола, нет display-aware tone/gamut mapping и нет гарантии, что XR30 действительно уходит по проводу как 10 bpc.

Моя приблизительная оценка:

| Область | Состояние |
|---|---:|
| Основная HDR-математика для opaque RGB | 8/10 |
| Внутреннее пространство композиции | 9/10 |
| Wayland color-management | 6/10 |
| Tone/gamut mapping | 2/10 |
| KMS metadata и физический сигнал | 4/10 |
| HDR screenshot/JXL | 8/10 |
| HDR/SDR interoperability | 4/10 |
| Общая production-готовность | около 5/10 |

## Что уже сделано хорошо

Внутренняя сцена — `R16G16B16A16_SFLOAT`, linear BT.2020, где `1.0 == 1 nit`: [renderer.cpp](/home/pg/monorepo/imway/renderer.cpp:184). Это очень хорошая нормализация. Она позволяет без двусмысленности смешивать SDR UI с HDR-контентом, сохранять абсолютную яркость и не привязывать сцену к конкретному монитору. Подобный подход согласуется с рекомендациями ITU по FP16 HDR-композиции и reference white около 203 nit: [ITU-R BT.2408](https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2408-8-2024-PDF-E.pdf).

PQ декодируется в абсолютные nits, SDR декодируется из sRGB, переводится BT.709 → BT.2020 и умножается на пользовательский SDR white. HDR-контент при изменении SDR white не меняет яркость — это правильно и уже проверяется тестом.

Финальный transform тоже концептуально правильный:

- HDR: linear BT.2020 nits → PQ → XR30.
- SDR: BT.2020 → BT.709 → нормализация относительно SDR white → sRGB.

Он находится в [output_transform.frag](/home/pg/monorepo/imway/output_transform.frag:29).

HDR scanout использует XR30, `BT2020_RGB` и `HDR_OUTPUT_METADATA`; hardware cursor в HDR заменяется software cursor: [device_kms.cpp](/home/pg/monorepo/imway/device_kms.cpp:1024).

Скриншотилка тоже сильная:

- завершённый scanout передаётся viewer’у как dma-buf;
- compositor переключается на новый scanout;
- нет обычного копирования полного 32-МиБ кадра;
- владение памятью корректно удерживается dma-buf fd;
- JXL сохраняется lossless, в PQ/BT.2100, с 16-битным контейнером для 10-битных данных: [main_screenshot.cpp](/home/pg/monorepo/imway/main_screenshot.cpp:257).

Viewer правильно просит `VK_COLOR_SPACE_HDR10_ST2084_EXT`. Для Wayland WSI Vulkan сам управляет `wp_color_management_surface_v1`, поэтому отдельная ручная реализация протокола viewer’ом не нужна: [Vulkan WSI specification](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html).

## Главный отсутствующий блок: display mapping

Сейчас HDR-сцена просто PQ-кодируется вплоть до 10 000 nit. Реальная способность дисплея не учитывается.

Нет:

- tone mapping по peak luminance дисплея;
- gamut mapping BT.2020 → реальный gamut панели;
- отдельного поведения для HDR-контента на SDR output;
- защиты цвета и hue при компрессии highlights;
- адаптации под full-frame luminance.

Это означает, что highlights либо клипуются монитором, либо монитор применяет неизвестный proprietary tone mapping. SDR output сейчас просто жёстко режет HDR выше SDR white.

В Linux именно compositor отвечает за приведение всех поверхностей к общему пространству и display mapping: [DRM/KMS HDR documentation](https://docs.kernel.org/gpu/drm-kms.html). Базовые алгоритмические ориентиры — [ITU-R BT.2390](https://www.itu.int/pub/R-REP-BT.2390-12-2025) и [BT.2446](https://www.itu.int/pub/R-REP-BT.2446).

Практически я бы ориентировался на KWin: tone mapping в ICtCp, сохранение reference anchor, компрессия яркости до реального display peak и отдельное gamut mapping.

## Мы почти ничего не знаем о дисплее

`setupHdr()` проверяет наличие KMS properties и XR30 plane, но не разбирает EDID/DisplayID: [device_kms.cpp](/home/pg/monorepo/imway/device_kms.cpp:1372).

Текущий монитор действительно сообщает:

- PQ;
- HLG;
- BT.2020 RGB/YCC.

Но luminance bytes в EDID отсутствуют. Следовательно, нынешние 1000 nit — не характеристика дисплея, а придуманная константа.

Нужно через libdisplay-info получить:

- поддержанные EOTF;
- colorimetry;
- min/peak/full-frame luminance, если присутствует;
- native primaries;
- режимы и ограничения link bandwidth.

При отсутствии luminance metadata нужна конфигурация/калибровка пользователя.

## KMS metadata сейчас несогласована с пикселями

Всегда выставляется примерно:

- mastering max 1000;
- min 0.0001;
- MaxCLL 1000;
- MaxFALL 400.

Но shader способен выдать сигнал до 10 000 nit. То есть metadata говорит «максимум 1000», а пиксели могут утверждать обратное.

Правильная последовательность:

1. Определить target display volume.
2. Собрать content metadata видимых поверхностей.
3. Выполнить tone/gamut mapping в target volume.
4. Выставить metadata, согласованную с полученным output.

Полностью доверять MaxCLL приложений нельзя, но declared metadata можно использовать как исходную подсказку.

## XR30 ещё не означает 10 bpc на проводе

Framebuffer XR30 гарантирует только формат буфера. Driver может вывести его через 8 bpc link из-за bandwidth, режима или настроек connector.

Сейчас imway не управляет:

- `max bpc`;
- фактическим link bpc;
- RGB full/limited range;
- fallback при недостаточной полосе;
- DSC/chroma/mode policy.

Нужно делать atomic test modeset с требуемыми 10/12 bpc и проверять фактический результат. В свежем DRM сейчас как раз развивается `link bpc` feedback, потому что одного `max bpc` недостаточно.

## Wayland-протокол всё ещё ограничен

Сейчас поддерживаются:

- sRGB;
- PQ;
- BT.709/sRGB primaries;
- BT.2020 primaries;
- parametric descriptions;
- полноценные output/preferred image descriptions, 64-битные identities и change events;
- perceptual intent — номинально.

Нет:

- HLG;
- extended linear/scRGB;
- BT.1886/gamma 2.2;
- Display P3;
- custom primaries;
- ICC profiles;
- Windows scRGB/BT.2100;
- `color-representation-v1`;

Отдельный большой пробел — YUV. Нет NV12/P010, matrix coefficients, chroma siting, full/limited range. HDR video вынужден заранее конвертироваться в RGB, теряя zero-copy и потенциально качество. Разделение ответственности между color-management и color-representation описано в [Wayland color management model](https://wayland.freedesktop.org/docs/book/Color.html).

## Производительность

Каждая color-managed surface получает отдельный полный `RGBA16F` conversion image, а conversion dispatch выполняется снова на каждом rendered frame.

Для 4K один такой image занимает около 63 МиБ. Triple-buffered HDR surface легко добавляет около 190 МиБ плюс bandwidth полного чтения/записи на каждый кадр.

Лучший путь:

- common RGB decode/primaries/reference mapping встроить непосредственно в composition fragment shader;
- передавать color description как per-texture параметры;
- intermediate image оставлять только для LUT, сложных YUV conversion и действительно кешируемых операций;
- повторно конвертировать только при появлении нового client buffer.

Это одновременно уменьшит latency, VRAM и memory bandwidth.

## ImGui и UI

Linear-light blend внутренней сцены физически корректен. Это хорошая политика, хотя KWin сознательно использует nonlinear blend для более привычного вида desktop UI.

Но screenshot viewer сейчас рисует ImGui непосредственно поверх PQ-encoded swapchain. Fixed-function blending происходит над PQ code values, а не linear light. Crop overlays и прозрачный UI там имеют неправильную яркость.

Viewer тоже должен иметь небольшой FP16 linear/nits intermediate:

```text
HDR image + SDR UI
→ linear blend
→ PQ encode
→ swapchain
```

## Screenshots

JXL-путь хороший, но стоит дополнить:

- `intensity_target`;
- `min_nits`;
- явной информацией о фактическом target volume;
- проверкой декодированных pixel values, а не только тегов.

PNG clipboard fallback сейчас плох: PQ XR30 просто ужимается до 8 бит и записывается без HDR metadata и без tone mapping. Обычный PNG viewer воспримет это как SDR и покажет неверно.

Правильно:

- JXL — HDR master;
- PNG clipboard — tone-mapped и gamut-mapped SDR sRGB с корректным profile/chunks;
- при желании отдельно добавить современный HDR PNG/cICP, но не использовать его как compatibility fallback.

## Night light и brightness

Night light сейчас — RGB-множители в linear BT.2020. Это не полноценная chromatic adaptation:

- меняется экспозиция;
- возможен gamut clipping;
- absolute HDR luminance дрейфует;
- не учитываются primaries реального output.

Нужен CAT16/Bradford-подобный transform в XYZ либо общий output color pipeline.

Настройка SDR white в nits математически реализована правильно, но физически эти nits не гарантированы: аппаратная яркость монитора меняется независимо. Для настоящего результата нужен output brightness/headroom policy и хотя бы пользовательская калибровка.

## Где сейчас SOTA

Наиболее полезные ориентиры:

- **KWin** — лучший desktop reference: per-output profiling, brightness/headroom, HDR-on-SDR, ICtCp tone mapping, color pipeline abstraction.
- **Gamescope** — gaming/HDR reference: HDR10/scRGB, inverse tone mapping, LUT, metadata, direct scanout: [Gamescope](https://github.com/ValveSoftware/gamescope).
- **Weston** — reference implementation Wayland-протоколов и KMS color pipeline.
- **DRM Color Pipeline API** — направление для аппаратного offload matrix/1D LUT/3D LUT с shader fallback: [kernel Color Pipeline API](https://docs.kernel.org/gpu/rfc/color_pipeline.html).

imway уже ближе к Gamescope/KWin по внутренней модели сцены, чем к простому «HDR-флагу на output». Отставание сейчас не в фундаменте, а в полноте color pipeline вокруг него.

## Рекомендуемый порядок доведения

1. EDID/libdisplay-info, overrides, реальный target display volume, bpc/range negotiation.
2. Реализовать tone mapping и gamut mapping.
3. Согласовать HDR_OUTPUT_METADATA с результатом mapping.
4. Убрать постоянные полноэкранные RGBA16F surface conversions.
5. Добавить `color-representation-v1`, P010/NV12, HLG и scRGB.
6. Dithering, корректный night light, linear screenshot viewer и SDR PNG fallback.
7. Затем аппаратный DRM color pipeline и безопасный HDR direct scanout.

Профильные HDR-тесты пока не покрывают tone/gamut mapping, output metadata, link depth и реальную фотометрическую корректность.

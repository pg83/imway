# PLAN

Диалог по задаче «аудит покрытия Wayland-протоколов» — verbatim из jsonl сессии.

## Статус исполнения

Вычёркивается по мере готовности; транскрипт-первоисточник — ниже.

### Этап A — версии реализованного
- [x] ~~wl_seat 5 → 11 (axis_value120, axis_relative_direction; warp deferred to pointer-warp)~~ done
- [x] ~~wl_compositor/wl_surface 4 → 6 (preferred_buffer_scale/transform, offset fix)~~ done
- [x] ~~wl_surface v7 get_release (per-commit buffer release callback)~~ done
- [~] wl_shm — capped at v1 by wl_display_init_shm (libwayland-gated, not imway source)
- [x] ~~wl_data_device_manager 3 → 4 (release)~~ done
- [x] ~~xdg_wm_base 3 → 7 (configure_bounds, wm_capabilities, suspended)~~ done
- [x] ~~zwp_linux_dmabuf_v1 4 → 5~~ done (v6 set_sampling_device gated by installed wayland-protocols v5)
- [x] ~~wp_presentation 1 → 2 (+ dropped false VSYNC on software timestamps)~~ done
- [x] ~~wp_cursor_shape 1 → 2 (zoom, dnd_ask, all_resize shapes)~~ done
- [x] ~~ext_idle_notify 1 → 2 (get_input_idle_notification)~~ done

### Этап B — дешёвая мелочь
- [x] ~~xdg-system-bell v1~~ done
- [x] ~~wp-alpha-modifier v1~~ done
- [x] ~~wp-pointer-warp v1~~ done
- [x] ~~xdg-wm-dialog v1~~ done
- [x] ~~wp-content-type v1~~ done

### Этап C — frame pacing
- [x] ~~wp-tearing-control v1~~ done (async page flip wired via DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP; hardware-unverified here)
- [x] ~~wp-fifo v1~~ done (per-surface queue of CommitCache snapshots, one admitted per presentation; barriers cleared for invisible surfaces)
- [x] ~~wp-commit-timing v1~~ done (target time rides the fifo queue entries; held entries keep the frame clock alive)

### Этап D — захват экрана
- [x] ~~ext-image-capture-source + ext-image-copy-capture v1~~ done (shm output sessions via the renderer readback; cursor sessions deliver the client cursor surface; dmabuf tranche later)
- [x] ~~zwlr-screencopy v3 (compat)~~ done (vendored XML in dev/protocols; same FrameCapture readback, region-capable)

### Этап E — клипборд
- [x] ~~ext-data-control v1~~ done (dc flag on the shared DataSource/Offer; focus-free set_selection, loopback offer, primary too)

### Этап F — текст/IME
- [x] ~~text-input-v3~~ done (per-seat, enter/leave on kb focus, full state relay to the IME)
- [x] ~~input-method-v2 + virtual-keyboard-v1~~ done (single IM per seat, commit/preedit/delete bridge, keyboard grab, vkbd injection)
- [x] ~~input-method popup~~ done (popup surface as a scene role at the cursor rectangle, screen-positioned relative to the app window)

### Сверх плана
- [x] ~~security-context-v1~~ done (sandbox tagging + privileged-global filter)
- [x] ~~tablet-v2~~ done (virtual pen: manager/seat/tool, proximity/down/motion/axes frames to the surface under the tool; the pen drives the shared cursor; libinput tablet-tool events wired; `tablet` control command + reg test)

### Этап G — полнота цвета
- [x] ~~mastering primaries/luminance, tf_power, named primaries~~ done (tf_power wired through the iccGamma path; mastering stored as target-volume metadata; 6 extra named primaries; get_output honest for the single output)

---


## pg

так, новая задача. аудит покрытия протоколов wayland нами и другими композиторами. качай код kwin/weston/wlroots/smithey/etc в monorepo/tmp/composers, аудит их покрытия, аудит нашего покрытия, мне - доклад.

## Claude

Начинаю: качаю исходники композиторов и эталонный wayland-protocols, затем скриптовый прогон покрытия по интерфейсам.

Матрица есть. Верифицирую подозрительные клетки и собираю точный список глобалов imway:

Материал собран и выверен (несколько клеток проверил вручную: kwin-захват — это приватный `zkde_screencast` через PipeWire, у smithay в master нет color-management, у weston нет fractional-scale и text-input-v3). Доклад.

# Аудит покрытия Wayland-протоколов: imway vs экосистема

**Метод**: shallow-клоны в `~/monorepo/tmp/composers` (weston, wlroots, kwin, smithay, mutter, Hyprland + эталонный wayland-protocols), grep серверных исходников по именам registry-глобалов 63 протоколов; для imway — точный список из `wl_global_create` (26 глобалов). Оговорки: wlroots и smithay — **библиотеки** (их клетка = «реализовано в библиотеке», конкретный композитор включает подмножество); kwin/mutter недосчитаны матрицей — часть функций у них в **приватных протоколах и порталах** (захват экрана — PipeWire, idle — D-Bus, у KDE ~20 протоколов plasma/kde, у GNOME gtk_shell1).

## Сводка

| | imway | weston | wlroots | kwin | smithay | mutter | Hyprland |
|---|---|---|---|---|---|---|---|
| покрытие | **26/63** | 22/63 | 56/63 | 43/63 | 46/63 | 35/63 | 54/63 |

imway уже покрывает больше эталонного weston. Все 7 реализуют один и тот же костяк (13 строк матрицы): ядро (compositor/shm/seat/output/subcompositor/DnD), xdg-shell, viewporter, presentation-time, single-pixel, dmabuf, xdg-output, pointer-constraints, relative-pointer. Дальше — различия.

## Матрица различий (строки, где не у всех одинаково)

`im we wr kw sm mu hy`; ✓/–:

```
shell     layer-shell            –  –  ✓  ✓  ✓  –  ✓
          xdg-dialog             –  –  ✓  ✓  ✓  ✓  ✓
          xdg-toplevel-drag      –  –  –  ✓  –  ✓  –
          xdg-toplevel-icon      ✓  –  ✓  ✓  ✓  –  –
          xdg-decoration         ✓  –  ✓  ✓  ✓  –  ✓
          xdg-system-bell        –  –  ✓  ✓  ✓  ✓  ✓
          xdg-session-mgmt       –  –  –  ✓  –  ✓  –
          xdg-foreign            –  –  ✓  ✓  ✓  ✓  ✓
present   fractional-scale       ✓* –  ✓  ✓  ✓  ✓  ✓   (* у нас заглушка 1.0)
          content-type           –  –  ✓  ✓  ✓  –  ✓
          tearing-control        –  ✓  ✓  ✓  –  –  ✓
          fifo                   –  ✓  –  ✓  ✓  ✓  ✓
          commit-timing          –  ✓  –  –  ✓  ✓  ✓
          alpha-modifier         –  ✓  ✓  ✓  ✓  –  ✓
buffers   drm-syncobj            ✓  –  ✓  ✓  ✓  ✓  ✓
          drm-lease (VR)         –  –  ✓  ✓  ✓  ✓  ✓
color     color-management       ✓  ✓  ✓  ✓  –  ✓  ✓
          color-representation   ✓  ✓  ✓  ✓  –  ✓  –
output    wlr-gamma-control      –  –  ✓  –  –  –  ✓
          wlr-output-mgmt/power  –  –  ✓  –  –  –  ✓
input     pointer-gestures       ✓  –  ✓  ✓  ✓  ✓  ✓
          cursor-shape           ✓  –  ✓  ✓  ✓  ✓  ✓
          kb-shortcuts-inhibit   ✓  –  ✓  ✓  ✓  ✓  ✓
          tablet-v2              –  ✓  ✓  ✓  ✓  ✓  ✓
          text-input-v3          –  –  ✓  ✓  ✓  ✓  ✓
          input-method-v2        –  –  ✓  –  ✓  –  ✓
          virtual-keyboard       –  –  ✓  –  ✓  –  ✓
          pointer-warp           –  –  ✓  ✓  ✓  ✓  ✓
clipbrd   primary-selection      ✓  –  ✓  ✓  ✓  ✓  ✓
          ext-data-control       –  –  ✓  ✓  ✓  –  ✓
session   ext-idle-notify        ✓  –  ✓  ✓  ✓  –  ✓
          idle-inhibit           ✓  –  ✓  ✓  ✓  ✓  ✓
          ext-session-lock       –  –  ✓  –  ✓  –  ✓
          xdg-activation         ✓  –  ✓  ✓  ✓  ✓  ✓
          security-context       –  –  ✓  ✓  ✓  –  ✓
capture   wlr-screencopy         –  –  ✓  –  –  –  ✓
          ext-image-copy-capture –  –  ✓  –  ✓  –  ✓
wm        foreign-toplevel(-list)–  –  ✓  –  ✓  –  ✓
          ext-workspace          –  –  ✓  –  –  –  ✓
```

Мёртвые/легаси (можно вычеркнуть навсегда): fullscreen-shell (0 у всех), zwp_linux_explicit_synchronization (только weston — вытеснен syncobj, который у нас есть), input-timestamps (только weston).

## Анализ: где мы стоим

**Сильная сторона — цвет.** color-management + color-representation есть только у нас, weston, wlroots, kwin, mutter — smithay (а значит niri/cosmic из коробки) и Hyprland (representation) отстают. Наш набор фич CM-протокола (parametric+ICC+luminances+scrgb+bt2100) — на уровне лидеров. Плюс полный «современный минимум» ввода (cursor-shape, gestures, shortcuts-inhibit), которого нет у weston.

**Пробел №1 — захват экрана: у нас ноль путей.** Ни wlr-screencopy, ни ext-image-copy-capture, ни портального пути (kwin/mutter закрывают это PipeWire-порталом). Следствие: не работают OBS, grim, любые скриншотилки и шаринг экрана в браузере — только наш встроенный скриншотер. Экосистемный стандарт сходится к **ext-image-copy-capture(+image-capture-source)** — оно у wlroots, smithay, Hyprland. Наш interactive-screenshot-путь (handoff сканаута) — отличный фундамент для реализации.

**Пробел №2 — игры/видео (дёшево закрывается).** tearing-control, fifo, commit-timing, content-type, alpha-modifier — «новая четвёрка+» frame-pacing, у Hyprland/kwin/smithay уже вся. Для single-user геймерского десктопа tearing+fifo — самое ощутимое (латентность fullscreen), content-type — подсказка для нашего же direct-scanout/VRR будущего, alpha-modifier — тривиален (композитор-side opacity, у нас всё равно ImGui-фейды).

**Пробел №3 — ввод текста.** text-input-v3 нет ни у нас, ни у weston — у всех остальных есть. Без него нет IME (CJK, emoji-пикеры, экранные клавиатуры GTK/Qt-приложений). Полный стек — text-input-v3 + input-method-v2 + virtual-keyboard. Если CJK не нужен — низкий приоритет, но это самый частый упрёк «неполноценного» композитора.

**Пробелы «чужого UI» — вопрос философии, а не техники.** layer-shell, foreign-toplevel, ext-workspace, wlr-output-management, ext-data-control, ext-session-lock — протоколы для *сторонних* панелей, доков, баров, локеров, клипборд-менеджеров. imway всё это рисует сам (ImGui-chrome, свой док, свой lockscreen). Тут решение принципиальное: либо экосистема waybar/swaylock/wl-clipboard (тогда layer-shell + data-control + session-lock), либо «батарейки внутри» — и тогда эти протоколы не нужны вовсе. kwin и mutter, замечу, выбрали второе (у них тоже нет wlr-панельных протоколов).

**Мелочи для совместимости приложений** (дёшево, по мере жалоб): xdg-dialog (модальные диалоги — есть у всех кроме нас и weston), xdg-foreign (порталы прикрепляют диалоги к чужим окнам — нужен для flatpak file-chooser'ов), pointer-warp (свежий, все взяли), system-bell. tablet-v2 — если появится планшет. security-context + drm-lease — flatpak-изоляция и VR, по потребности.

**Заглушка fractional-scale** — единственное место, где мы «рекламируем и не делаем» (шлём 1.0 всегда); это хуже отсутствия и уже помечено xfail.

## Рекомендуемый порядок (под наш профиль: single-user, свой chrome, качество картинки)

1. **ext-image-copy-capture + source** — единственный пробел, который блокирует целые классы приложений (OBS/шаринг); фундамент уже есть.
2. **tearing + fifo + content-type** — прямой выигрыш латентности fullscreen-игр и подсказки нашему direct scanout.
3. **Честный fractional-scale** (или убрать глобал до готовности — сейчас врём).
4. **xdg-dialog, xdg-foreign, pointer-warp, alpha-modifier, system-bell** — пачка мелких «как у всех», каждый на день.
5. **text-input-v3** (без input-method-v2 на первом шаге: достаточно для GTK/Qt-полей с系统ной раскладкой) — когда/если понадобится IME.
6. layer-shell/data-control/session-lock — только если решишь пускать сторонний UI.

Исходники лежат в `~/monorepo/tmp/composers` (сегодняшние master-снапшоты), скрипт матрицы — в scratchpad (`protocov.sh`), могу перегнать с другим набором протоколов.

## pg

Заглушка fractional-scale — единственное место, где мы «рекламируем и не делаем» (шлём 1.0 всегда); это хуже отсутствия и уже помечено xfail. - пока так и оставляем. Пробелы «чужого UI» — вопрос философии, а не техники. layer-shell, foreign-toplevel, ext-workspace, wlr-output-management, ext-data-control, ext-session-lock — протоколы для сторонних панелей, доков, баров, локеров, клипборд-менеджеров. imway всё это рисует сам (ImGui-chrome, свой док, свой lockscreen). Тут решение принципиальное: либо экосистема waybar/swaylock/wl-clipboard (тогда layer-shell + data-control + session-lock), либо «батарейки внутри» — и тогда эти протоколы не нужны вовсе. kwin и mutter, замечу, выбрали второе (у них тоже нет wlr-панельных протоколов). - идем по пути kwin. Пробел №3 — ввод текста. text-input-v3 нет ни у нас, ни у weston — у всех остальных есть. Без него нет IME (CJK, emoji-пикеры, экранные клавиатуры GTK/Qt-приложений). Полный стек — text-input-v3 + input-method-v2 + virtual-keyboard. Если CJK не нужен — низкий приоритет, но это самый частый упрёк «неполноценного» композитора. - надо делать в итоге. Важно - даже если нам протокол не нужен, но его изготовить легко, его стоит сделать, потому что будут сравнивать же (wlr панели не нужны даже если просто). Исходя из этого пиши план изготовления, по пунктам. Важно - если поддерживаем, то надо поддержать максиально полно, насколько это у нас вообще возможно.

## Claude

План соберу с учётом принципа «поддерживаем — так максимально полно»: это касается и **версий уже реализованных** глобалов. Сниму текущие версии против последних:

# План изготовления протокольного покрытия

Принципы для каждого пункта (наш устоявшийся конвейер): красный тест до кода (`client_reg_*`/`client_err_*` + headless-сценарий, негативы на каждую protocol error — по образцу существующих 60+ err-тестов), версия глобала — максимальная существующая, состояние наблюдаемо через `dump_state`. Оценки: S — до дня, M — дни, L — неделя+.

## Этап A — догнать версии уже реализованного (принцип «полноты», почти всё S)

| Глобал | у нас → латест | что конкретно добавляется |
|---|---|---|
| `wl_seat` | 5 → **11** | pointer v8 `axis_value120` (hi-res колесо из libinput), v9 `axis_relative_direction`, v11 `warp` event (связка с этапом B-3); keyboard/touch — release-запросы. **M** (v120 надо пробросить от libinput через InputSink до wayland) |
| `wl_compositor`/`wl_surface` | 4 → **7** | v6 `preferred_buffer_scale/transform` (шлём 1/normal — честно, у нас scale=1), v7 `wl_surface.get_release` — per-buffer release, снимает гонки переиспользования буферов; ложится на наш FrameResource. **M** |
| `wl_shm` | 1 → 3 | release-запросы, libwayland сам. **S** |
| `wl_data_device_manager` | 3 → 4 | release. **S** |
| `xdg_wm_base` | 3 → **7** | v4 `configure_bounds` (у нас есть workW/H — слать), v5 `wm_capabilities` (заявить window-menu/maximize/fullscreen/minimize — всё умеем), v6 `suspended` state (свёрнутые/невидимые окна — у нас есть minimized), v7 edge-constraints. **M** |
| `zwp_linux_dmabuf_v1` | 3/4 → **6** | v5 бамп, v6 `set_sampling_device` + feedback-нюансы; транш scanout уже сделан. **S** |
| `wp_presentation` | 1 → 2 | refresh=0 для VRR-кейса, бамп. **S** (+ снять враньё `VSYNC` на софтверном таймстампе — однострочник туда же) |
| `wp_cursor_shape` | 1 → 2 | новые формы enum'а в маппинг CursorKind. **S** |
| `ext_idle_notify` | 1 → 2 | `get_input_idle_notification` (таймер без учёта inhibitors — у нас вся machinery есть). **S** |

Итого этап A: ~неделя, поднимает «паспортные» версии до уровня wlroots.

## Этап B — дешёвая мелочь «как у всех» (каждый S, суммарно ~неделя)

1. **xdg-system-bell v1** — `ring(surface)` → тост/подсветка окна через наш Notifier.
2. **wp-alpha-modifier v1** — множитель альфы поверхности; у нас это буквально ImGui-tint в `drawSurfaceTree` (умножить в `In.Color.a`). Учесть в direct scanout (alpha<1 → не кандидат — уже есть opaque-механика).
3. **wp-pointer-warp v1** — запрос → `posX/posY` (проверки: фокус поверхности + валидный serial), плюс `wl_pointer.warp` event из A. Тест через control+клиент.
4. **xdg-wm-dialog v1** — флаг modal на toplevel: рисуем поверх родителя, фокус не отдаём родителю, dump-поле.
5. **wp-content-type v1** — хранить тип (none/photo/video/game); первый потребитель — наш scanout-кандидат (video/game → приоритет bypass) и будущий tearing. 

## Этап C — игры/видео (frame pacing)

6. **wp-tearing-control v1** — протокольная часть S (хинт на поверхности); реальная работа — **async page flip** в KMS direct scanout (`DRM_MODE_PAGE_FLIP_ASYNC` в atomic, фолбэк на vsync при отказе). Активировать только: fullscreen + direct scanout + хинт async. **M**, железная проверка.
7. **wp-fifo v1** — барьеры «кадр показан» для очереди коммитов; требует очереди отложенных коммитов per-surface (у нас commit применяется сразу — появится механизм latch/queue). **M–L** (машинерия переиспользуется в 8 и в будущем text-input… нет, в commit-timing).
8. **wp-commit-timing v1** — таймированные коммиты (target presentation time) поверх той же очереди из п.7. **M** (после 7).

Порядок жёсткий: 7 → 8; 6 независим.

## Этап D — захват экрана (главный экосистемный пробел, L)

9. **ext-image-capture-source v1 (output source)** + **ext-image-copy-capture v1**:
   - сессия захвата: buffer constraints (shm + dmabuf наших форматов), frame с damage, cursor-режимы (hidden/embedded/standalone — у нас все данные есть: composited сцена + отдельный курсор);
   - однократные кадры — прямое переиспользование нашего screenshot/readback пути (blit сканаута → клиентский буфер); непрерывный захват — по frame-clock с damage-координацией;
   - toplevel-source **не делаем** (требует ext-foreign-toplevel-list — вне kwin-пути), капабилити просто не заявляем.
   - Тесты: headless клиент-захватчик, пиксельная сверка со screenshot'ом; негативы на constraints.
10. **zwlr-screencopy v3 (compat)** — тонкая обёртка над тем же ядром захвата, ради сегодняшних grim/wf-recorder. **M** после 9. (Это тулинг, не «wlr-панель» — kwin закрывает нишу порталом, у нас портала нет.)

## Этап E — клипборд-менеджеры

11. **ext-data-control v1** (kwin его имеет — путь совместим): привилегированные selection/primary-selection без фокуса; переиспользует DataSource/Offer-машинерию. Решить и зафиксировать политику доступа (пока: доверяем локальным клиентам, как kwin без ограничений). **M**. `zwlr_data_control` — не делаем (ext — преемник; wl-clipboard/cliphist умеют ext).

## Этап F — текст/IME (финальный крупный блок, строго по фазам)

12. **text-input-v3** — per-seat объект, enter/leave по фокусу клавиатуры, приём state (surrounding, cursor rect, content-type), done-serials. Без IM-бэкенда протокол «пустой», но honest: приложения видят enter/leave. **M**.
13. **input-method-v2 + virtual-keyboard-v1** — один IM-клиент на seat (fcitx5), пересылка состояния text-input↔input-method, keyboard-grab (наш InputSink-конвейер это умеет — вставка sink'а выше wayland), virtual-keyboard для синтеза. **L**.
14. **input-method popup** — поверхность-кандидат у cursor rect: новая роль в сцене, позиционирование как у xdg_popup. **M**.

Тестируемость полная: тестовый IM-клиент в dev/tests (мы сами себе fcitz) — красные тесты пишутся без реального IME.

## Этап G — «полнота» уже поддержанного (цвет)

15. color-management: `set_mastering_display_primaries`/`set_mastering_luminance` (принимать и хранить — наш tone map может брать их как источник target-метаданных), `tf_power`, остальные named primaries (NTSC/PAL/DCI/AdobeRGB — это просто хроматики в таблицу), честный `get_output` по переданному wl_output. **M**. (`set_luminances` для sRGB/HLG — отложено по твоему решению «колориметрия потом».)

## Не делаем — зафиксировать письменно (kwin-путь)

layer-shell, foreign-toplevel(-list/-management), ext-workspace, wlr-output-management/-power, wlr-gamma-control, ext-session-lock, ext-transient-seat, fullscreen-shell (мёртв), zwp_linux_explicit_synchronization (вытеснен syncobj), input-timestamps. Предлагаю секцию в README: «протоколы стороннего chrome не реализуются осознанно: панель, док, локер и настройка выводов — встроенные» — чтобы сравнивающие видели решение, а не дыру. fractional-scale — остаётся заглушкой (решение зафиксировано).

## Сводный порядок и объём

A (версии, ~неделя) → B (мелочь, ~неделя) → C (pacing: fifo→commit-timing, tearing параллельно) → D (захват, самый большой выхлоп для сравнений) → E (data-control) → F (IME-стек) → G (цвет). Суммарно — порядка 6–8 недель календарно при нашем темпе, каждый пункт — отдельный red-first цикл и отдельный коммит.

Если порядок ок — начинаю с этапа A (он же самый механический и сразу двигает «паспортные» цифры сравнения).


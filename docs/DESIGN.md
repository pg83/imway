# imway — Wayland-композитор поверх Dear ImGui

Проектный документ. Состояние экосистемы — середина 2026.
§0–13 писались до кода; §14 описывает, что и как реализовано на самом деле.
При расхождении верить §14 — старые разделы остаются планом на будущие кольца.

## 0. База (зафиксировано)

- Окна wayland-клиентов встроены в ImGui-окна (текстура клиента = `ImGui::Image()`),
  весь хром (меню, панели, декорации) — ImGui. Docking даёт тайлинг бесплатно.
- **Стек: libwayland-server + libdrm (atomic KMS) + libinput + libev + xkbcommon.**
  Никаких wlroots/Louvre/Smithay — все протоколы реализуем сами.
  SDL3 — только для nested dev-режима, если вообще будет нужен.
- **Vulkan-only.** В нашем коде нет ни одного вызова OpenGL/EGL/GLES. ImGui через
  `imgui_impl_vulkan` (`ImTextureID` = `VkDescriptorSet`). Бонус Vulkan-пути: вся
  синхронизация явная — semaphore/sync_file/syncobj, никакой магии implicit sync
  драйвера, которая на GL «просто работает, кроме NVIDIA».
- Прямого прецедента «wayland-окна внутри ImGui» не существует — идея новая, но
  каждый блок проверен: QtWayland `QWaylandQuickItem` (окна как виджеты тулкита,
  наша модель — калька), gamescope (Vulkan-композитор с ImGui-оверлеями, сырой
  atomic KMS), kms-vulkan (минимальный «Vulkan рендерит, KMS сканирует» пример).

## 1. Vulkan → KMS: инициализация (главный открытый вопрос — решён)

Ключевой факт: **GBM не обязателен**. Есть две архитектуры; обе сводятся к
«рендерим в VkImage, известный KMS как dmabuf-FB, показываем atomic-коммитом».

### 1.1 Архитектура (a): Vulkan аллоцирует, KMS сканирует — путь gamescope. НАШ ВЫБОР

1. **Согласование модификаторов**: пересечение двух множеств —
   - KMS: parse блоба `IN_FORMATS` primary plane (гейт: `DRM_CAP_ADDFB2_MODIFIERS`);
   - Vulkan: `vkGetPhysicalDeviceFormatProperties2` + `VkDrmFormatModifierPropertiesListEXT`
     (напр. `DRM_FORMAT_XRGB8888` ↔ `VK_FORMAT_B8G8R8A8_UNORM`), каждый кандидат
     подтверждаем `vkGetPhysicalDeviceImageFormatProperties2` c
     `VkPhysicalDeviceImageDrmFormatModifierInfoEXT` + требуем
     `VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT`.
2. **Создание**: `vkCreateImage`, `tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT`,
   pNext: `VkImageDrmFormatModifierListCreateInfoEXT{пересечение}` +
   `VkExternalMemoryImageCreateInfo{DMA_BUF_BIT_EXT}`. Флага «scanout» в Vulkan нет —
   scanout-пригодность выражается только модификатором (на десктопных драйверах
   этого достаточно; ARM SoC с contiguity-требованиями — см. 1.2).
3. **Аллокация**: dedicated (`VkMemoryDedicatedAllocateInfo`) +
   `VkExportMemoryAllocateInfo{DMA_BUF_BIT_EXT}`.
4. **Экспорт**: `vkGetMemoryFdKHR` → dmabuf fd;
   `vkGetImageDrmFormatModifierPropertiesEXT` → какой модификатор выбрал драйвер;
   per-plane layout: `vkGetImageSubresourceLayout(VK_IMAGE_ASPECT_MEMORY_PLANE_i_BIT_EXT)`
   → offset/rowPitch (питчи руками не считать никогда).
5. **KMS**: на primary node `drmPrimeFDToHandle` → GEM handles →
   `drmModeAddFB2WithModifiers(…, DRM_MODE_FB_MODIFIERS)` → atomic commit `FB_ID`.
   Осторожно: GEM handles не рефкаунтятся ядром (повторный импорт того же буфера
   даёт тот же handle) — классический источник тихих багов.

2–3 таких flippable-образа на output. LINEAR-fallback держать (планы без
модификаторов, cursor plane, cross-GPU).

### 1.2 Архитектура (b): GBM аллоцирует, Vulkan импортирует — путь wlroots/kms-vulkan

`gbm_bo_create_with_modifiers2(SCANOUT|RENDERING)` → fd/offset/stride/modifier →
импорт в Vulkan (`VkImageDrmFormatModifierExplicitCreateInfoEXT` +
`VkImportMemoryFdInfoKHR`). Зачем: `GBM_BO_USE_SCANOUT` знает вендорные ограничения
размещения (contiguity на SoC), которые Vulkan выразить не может — именно поэтому
wlroots оставил GBM аллокатором по умолчанию.

**Решение**: (a) за тонким интерфейсом аллокатора (~4 функции), чтобы (b) можно было
подставить за ~150 строк, если упрёмся в железо. GBM выпадает из зависимостей.

### 1.3 Выбор устройства
`VK_EXT_physical_device_drm`: `VkPhysicalDeviceDrmPropertiesEXT{primary/renderMajor/Minor}`
сверяем с `fstat(kms_fd)` → major/minor. Требовать это расширение жёстко (как
gamescope). Кейс «render device ≠ display device» (Asahi и пр.) — помнить, не решать в v1.

### 1.4 Что НЕ использовать
`VK_KHR_display` / `vkAcquireDrmDisplayEXT` — прячет CRTC/планы/свойства за опаковым
свопчейном: без atomic-пропертей, курсорного плана, фенсов, VRR. Для VR/kiosk, не
для композитора. Все изученные проекты сходятся.

## 2. Синхронизация (вся явная)

- **Кадр → KMS**: submit сигналит (i) внутренний timeline semaphore (точка на кадр —
  им же трекаем лайфтаймы буферов/текстур вместо VkFence) и (ii) binary semaphore c
  `VkExportSemaphoreCreateInfo{SYNC_FD}` → `vkGetSemaphoreFdKHR` → sync_file →
  **`IN_FENCE_FD`** плана в atomic commit (`NONBLOCK`). Внимание: экспорт SYNC_FD
  сбрасывает binary semaphore — это фича, wlroots на неё опирается.
- **Реюз output-буфера**: по page-flip event (просто) или `OUT_FENCE_PTR` →
  `vkImportSemaphoreFdKHR(TEMPORARY)` (чисто; так делает kms-vulkan).
- **Implicit sync клиентов** (дефолт для wayland): мост через dma-buf ioctls (ядро ≥6.0):
  перед семплингом `DMA_BUF_IOCTL_EXPORT_SYNC_FILE` → временный импорт в binary
  semaphore → wait; после композита свой render-finished sync_file →
  `DMA_BUF_IOCTL_IMPORT_SYNC_FILE` в буфер клиента (его следующая запись подождёт наш
  рид) — и это же событие = момент `wl_buffer.release`. Рецепт дословно =
  `vulkan_sync_foreign_texture()` / `vulkan_sync_render_buffer()` из wlroots.
- **`wp_linux_drm_syncobj_v1`** (explicit-sync клиенты; NVIDIA без него — боль):
  гейт `DRM_CAP_SYNCOBJ_TIMELINE`; acquire point может не материализоваться на момент
  коммита — не блокироваться, ждать через `DRM_IOCTL_SYNCOBJ_EVENTFD` в event loop;
  GPU-wait портируемо: `drmSyncobjTransfer` → `drmSyncobjExportSyncFile` → импорт
  SYNC_FD; release: свой sync_file → `drmSyncobjImportSyncFile` → transfer в timeline
  клиента на release point.

## 3. Буферы клиентов → ImGui

- **wl_shm** (обязательный, первый): mmap пула; staging `VkBuffer` (host-visible) →
  `vkCmdCopyBufferToImage` по damage-прямоугольникам (`wl_surface.damage_buffer`) →
  барьеры `TRANSFER_DST → SHADER_READ_ONLY`. `wl_buffer.release` сразу после записи
  копии в командный буфер и его завершения (важно для single-buffered клиентов).
  SIGBUS-защита от клиента, обрезавшего fd. `nonCoherentAtomSize` при flush.
- **linux-dmabuf-v1 v4+** (GPU-клиенты; Mesa 25.2 удалила wl_drm — без dmabuf
  GPU-клиенты не работают вообще): feedback (format table в sealed memfd,
  `main_device` = **render node**, транши); импорт: `VkImageDrmFormatModifierExplicitCreateInfoEXT`
  + `VkImportMemoryFdInfoKHR` → `VkImageView`. Кэшировать VkImage per `wl_buffer`
  (клиенты гоняют 2–4 буфера по кругу). Релиз dmabuf-буфера — только когда timeline
  point последнего кадра, семплившего его, пройден.
- **ImGui**: `ImGui_ImplVulkan_AddTexture(view, layout)` → `VkDescriptorSet` =
  `ImTextureID`. Пул с `FREE_DESCRIPTOR_SET_BIT`, размер щедрый (~4096) — недобор
  пула фейлится недетерминированно по драйверам. `RemoveTexture`/destroy view —
  только после retire кадра (timeline point). Редизайн бэкенда 2026-04: раздельные
  SAMPLED_IMAGE + SAMPLER дескрипторы, `AddTexture` без параметра sampler —
  пин версии ImGui и чтение changelog при апгрейдах.
- **YUV (NV12 от видеоплееров)**: `VkSamplerYcbcrConversion` требует immutable
  sampler в layout — стоковый imgui_impl_vulkan это не умеет (а с раздельными
  дескрипторами — не умеет принципиально). Решение: свой маленький blit-pass
  YUV→RGBA (immutable-ycbcr-sampler pipeline) до ImGui; ImGui видит обычную RGBA.
  Так делают все. RGB-dmabuf (подавляющее большинство) работают напрямую.
- Layout-переходы импортированных образов — на нас, включая ручные барьеры
  `VK_QUEUE_FAMILY_FOREIGN_EXT` (нужен `VK_EXT_queue_family_foreign`).

Расширения (device): `VK_EXT_image_drm_format_modifier`, `VK_KHR_external_memory_fd`,
`VK_EXT_external_memory_dma_buf`, `VK_EXT_queue_family_foreign`,
`VK_KHR_external_semaphore_fd`, `VK_EXT_physical_device_drm`, timeline semaphores (1.2).

## 4. Event loop (libev)

Один поток. `wl_event_loop_get_fd()` — это epoll fd libwayland (единая точка):

- `ev_io` на wayland fd → `wl_event_loop_dispatch(loop, 0)`;
- `ev_io` на libinput fd → `libinput_dispatch` + раздача событий;
- `ev_io` на DRM fd → `drmHandleEvent` (page_flip_handler2, v3 — per-CRTC) —
  это frame clock каждого output;
- `ev_prepare` (перед сном): **`wl_display_flush_clients()`** — инвариант libwayland:
  не засыпать с несброшенными буферами клиентов (иначе классический дедлок);
- `ev_timer` — кадровый дедлайн / анимации / ping-таймауты;
- syncobj eventfd'ы (acquire points) — тоже `ev_io`.

Рендер — по требованию: кадр рисуем, если (commit с damage) ∨ (input) ∨ (анимация
ImGui) ∨ (должны frame callbacks). Идеальный idle = 0 fps. ImGui перегенерирует всю
геометрию каждый кадр — по-пиксельный damage output'а не для нас
(`LOAD_OP_DONT_CARE`, полный redraw), но client-side damage обязателен: для
shm-загрузок и для решения «рисовать ли кадр вообще».

**Frame callbacks — контракт**: `wl_surface.frame done()` шлём по page-flip только
поверхностям, реально показанным в кадре. Невидимым — не шлём (это троттлинг),
видимым — обязательно (иначе клиент замерзает). Слишком рано (сразу на commit) —
клиенты крутятся на 1000+ fps.

## 5. Input

### 5.1 Два потребителя
Каждое событие форкается: ImGui (переведённые ImGuiKey, позиция курсора) и клиенты
(**сырые evdev-коды** + surface-local координаты). libinput отдаёт evdev-коды и
ускорение из коробки. xkbcommon у нас — для ImGui-текста (`xkb_state_key_get_utf8`)
и хоткеев; клиентам шлём keymap fd (memfd, sealed) + `wl_keyboard.modifiers` после
каждого enter и при изменении сериализации. Автоповтор: для ImGui делаем сами
(на железе его нет), клиенты повторяют сами по `repeat_info(25, 600)` — сами НЕ
повторяем. LED'ы клавиатуры (`libinput_device_led_update`) не забыть.

Nested/SDL3: `SDL_KeyboardEvent.raw` (3.2+) — evdev-код на wayland-бэкенде (на X11
это X keycode = evdev+8, нормализовать); фильтровать `event.key.repeat`; keymap
родителя забрать своим bind'ом wl_seat через `SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER`.

### 5.2 Маршрутизация фокуса (здесь живут все «input сломан»)
Единая функция с приоритетами:
1. активный grab клиента (popup grab; кнопка зажата внутри окна — latch до button-up);
2. попапы/модалки ImGui;
3. hovered встроенная поверхность — **`ImGui::IsItemHovered()` на image-итеме**
   (не `io.WantCaptureMouse` — тот не отличает «над клиентом» от «над тайтлбаром»);
4. виджеты ImGui.

Клавиатурный фокус: `wl_keyboard.enter` (с массивом зажатых клавиш!) когда ImGui-окно
поверхности в фокусе и `io.WantTextInput == false`; синхронизировать со state
`activated`. Хоткеи композитора — до всего.

### 5.3 Координаты (три поправки, иначе клики мимо на ~30px в GTK)
item rect → масштаб текстуры; + offset `xdg_surface.set_window_geometry` (CSD-тени
вне geometry); + `buffer_scale` / viewport. `wl_pointer.frame` после каждой логической
группы (seat v5+). Кнопки — evdev (`BTN_LEFT=0x110`). Скролл: `axis` (15/деление) +
`axis_value120`.

### 5.4 Курсоры
- `cursor-shape-v1` рано (enum → наш курсор, без рендера серфейсов);
- легаси `wl_pointer.set_cursor(surface)`: над встроенным окном рисуем поверхность
  курсора на `ImGui::GetForegroundDrawList()` в `mouse_pos - hotspot` (это обычная
  поверхность: commits, frame callbacks, анимация); свой курсор прячем.
- Курсор на KMS: сначала композитный (последний draw ImGui), потом cursor plane —
  cursor-only atomic commit на каждый motion = латентность уровня X11 независимо
  от fps композитора.

## 6. xdg-shell (реализуем сами) — критические места

- **Configure dance**: первый configure `0×0` («сам выбери размер»), дальше размер
  диктует content region ImGui-окна; буфер до первого configure — protocol error
  (`unconfigured_buffer`); ресайз — state `resizing`, не чаще кадра, старую текстуру
  масштабируем пока клиент не ack'нул; `ack_configure` — точка синхронизации сериалов.
- **window geometry ≠ размер буфера**: кроп UV по geometry; размеры в configure —
  geometry; анкоры попапов — geometry-relative.
- **Декорации**: `zxdg_decoration_manager_v1` → `server_side`. Qt6 уберёт CSD,
  GTK4 протокол не реализует — хедербар останется (принять).
- **Попапы**: позиционер (anchor/gravity/constraint_adjustment: flip→slide→resize),
  configure с координатами **относительно geometry родителя** — поэтому при
  перетаскивании ImGui-окна попапы едут следом бесплатно (клиент не знает, где его
  toplevel). Рисовать на `GetForegroundDrawList()` (не внутри родительского окна —
  клиппинг, z-order); constraints решать против всего экрана; reactive (v3+)
  переconfigure'ивать. Grab: клик вне поверхностей клиента (включая ImGui-хром) ⇒
  `popup_done`, и этот клик не должен сработать в ImGui.
- **Субповерхности — не опция** (mpv, Firefox, GL/Vulkan-области тулкитов): toplevel —
  дерево поверхностей. Композитим дерево в offscreen VkImage на commit → ImGui
  получает одну текстуру; hit-test по дереву; sync/desync коммиты по спеке.
- `ping/pong` с таймером; `set_title/app_id` → заголовок (`"%s###%p"` — стабильный
  ID); `set_min/max_size` → `SetNextWindowSizeConstraints`; `close()` — по крестику.

## 7. Протоколы по ярусам (всё пишем сами)

- **Tier 0**: `wl_compositor`, `wl_subcompositor`, `wl_shm`, `wl_seat` (v≥5, лучше 8–9),
  `wl_output`, `xdg_wm_base` (Qt6 без него abort), `wl_data_device_manager`
  (Qt/GTK биндят на старте; клипборд/DnD).
- **Tier 1**: `linux-dmabuf-v1` v4 (фактически tier 0 для GPU-клиентов),
  `zxdg_decoration_manager_v1`, `wp_viewporter` (кроп/скейл = UV/размер ImGui::Image),
  `wp_presentation` (timestamps с page-flip; mpv A/V-sync), `xdg-output`,
  `primary_selection`, `xdg_activation_v1`, `fractional-scale-v1`,
  `single-pixel-buffer-v1` (тривиален), `wp_linux_drm_syncobj_v1`.
- **Tier 2**: relative-pointer + pointer-constraints (игры), idle-inhibit,
  text-input-v3 (IME), pointer-gestures.
- **XWayland: делегировать** — xwayland-satellite (нужны лишь xdg_wm_base +
  viewporter + xdg-output): X11-приложения приходят обычными wayland-toplevel'ами.
  Нативная интеграция (быть X11 WM поверх xcb) — многомесячный подпроект, не наш.

## 8. Сессия и bare metal

- **libseat** (seatd/logind): fd'ы устройств без root, DRM master drop/regain на
  VT-switch. Это не фреймворк — брокер fd. Альтернатива на первое время: запуск
  с своей TTY/root'ом и прямой `drmSetMaster`.
- **VT-switch**: на disable — стоп frame clocks, `libinput_suspend()`, отпустить
  зажатые клавиши клиентам, `libseat_disable_seat()` (обязательный ack). На enable —
  всё KMS-состояние считать потерянным: полный remodeset `ALLOW_MODESET`.
  Тестировать сотнями переключений.
- **Atomic-only**: `DRM_CLIENT_CAP_ATOMIC` (+`UNIVERSAL_PLANES`), property-based,
  `TEST_ONLY` для валидации, один in-flight commit на CRTC (`EBUSY`).
- **udev**: монитор `drm` subsystem (change/HOTPLUG → рескан коннекторов);
  input-hotplug делает libinput сам.
- **Мульти-монитор**: один DRM fd / VkDevice; per-output: свой swapchain из 2–3
  flippable-образов + свой repaint loop от своих flip-событий (частоты разные).
  ImGui: один контекст на output (общий `ImFontAtlas`), multi-viewport не использовать.
- **Overlay planes / direct scanout**: пропустить — у нас всегда полный composite.
  Вернуться только для fullscreen-кейса.
- **NVIDIA quirks file** заранее: dmabuf c `MOD_INVALID` не импортируется, LINEAR
  sampled-only (не renderable), `IN_FENCE_FD` может дать `-EPERM` (gamescope
  ретраит без него), syncobj-протокол обязателен.

## 9. Nested dev-режим

Абстракция, проверенная gamescope: **рендерер рисует в VkImage, чью презентацию не
владеет**; два бэкенда Output:
- bare metal: dmabuf-экспортированные образы + atomic KMS (flip events = frame clock);
- nested: SDL3 → `SDL_Vulkan_CreateSurface` → `VkSwapchainKHR`
  (acquire semaphore → render → present). Ничего экзотического.

Код render-pass'а таргетит «VkImageView + extent», не «свопчейн».

## 10. Роадмап

1. **M0 — Vulkan/KMS спайк** (отдельно от композитора, по образцу kms-vulkan):
   libseat/root → atomic modeset → VkImage-аллокация с модификаторами → dmabuf →
   AddFB2 → flip loop c семафорами в обе стороны → чистый VT-switch. Треугольник/
   ImGui-демо на голом железе. Это де-рискует самое мутное место сразу.
2. **M1 — скелет композитора** (nested SDL3+Vulkan или сразу поверх M0): libev-цикл,
   wl_display, `wl_shm` + `xdg_toplevel` (configure dance) → текстура в ImGui-окне.
   Критерий: **foot запускается и виден**.
3. **M2 — input**: seat, единая маршрутизация (§5.2), keymap, курсоры.
   Критерий: foot юзабелен (набор, выделение, скролл).
4. **M3 — настоящие клиенты**: `linux-dmabuf-v1` v4 + implicit-sync мост,
   субповерхности (композиция дерева в VkImage), decoration, viewporter.
   Критерий: mpv, GTK4/Qt6 с GPU-рендером.
5. **M4 — попапы/grab'ы, clipboard/DnD, presentation-time, fractional-scale.**
   Критерий: меню/комбобоксы везде, копипаст между клиентами.
6. **M5 — бытовое**: syncobj (NVIDIA), YUV blit-pass, cursor plane, hotplug,
   мульти-монитор, xwayland-satellite, render-on-demand экономия.

## 11. Топ рисков

1. **Vulkan-аллокация scanout на нестандартном железе** — модификатор задаёт layout,
   но не placement (contiguity на SoC); страховка — интерфейс аллокатора с
   GBM-запасным путём (~150 строк).
2. **Объём протокольной работы** — всё, что wlroots давал бесплатно (xdg-shell
   state machine, dmabuf feedback, data-device), теперь наше. Compensating: полный
   контроль и Vulkan-нативность.
3. **Арбитраж фокуса ImGui ↔ клиенты** — одна функция маршрутизации, item-level hit-test.
4. **Configure/ack/geometry машина состояний** — ошибки = misclick'и и protocol kill.
5. **Дисциплина event loop** — dispatch → render → callbacks → flush → sleep; не
   засыпать без flush; не забывать callbacks видимым.
6. **imgui_impl_vulkan движется** (редизайн дескрипторов 2026-04) — пин версии.
7. **NVIDIA** — quirks, syncobj рано.
8. **GEM handle lifetime** и **сброс semaphore при экспорте SYNC_FD** — два
   классических источника тихих багов.

## 12. Дев-харнесс (4 кольца)

Хост разработки — macOS; target — stal/ix (полностью статическая линковка, dlopen
заменён статической dlfcn-фабрикой ⇒ любой Mesa/Vulkan-драйвер, включая lavapipe,
линкуется в бинарь напрямую; ОС-монорепо: /Users/pg/monorepo/ix). Всё нужное в ix
уже есть: lib/{drm,ev,evdev,input,mesa,seat,udev,vulkan,wayland,xkb},
bin/{sway,labwc,weston,foot,dwl,qemu}; `lib/mesa/soft` = lavapipe.

- **Кольцо 0 — macOS нативно (секунды)**: ядро логики платформонезависимо и
  тестируется юнитами без libwayland (configure state machine, positioner solver,
  focus router, damage). Shell/рендер: ImGui+Vulkan работает на маке через MoltenVK
  (SDL3) — весь хром (окна/меню/докинг/фокус) разрабатывается на fake-клиентах
  (текстуры-заглушки, синтетический input) без Linux. Здесь же гоняются
  автономные тесты при разработке.
- **Кольцо 1 — Linux VM на маке (QEMU aarch64 + hvf), основной integration loop**:
  Debian trixie cloud-образ + cloud-init (vm/create.sh — полностью скриптованный
  provision), rsync исходников + сборка/тесты внутри (./build.sh). ssh внутрь;
  крашится процесс, не машина. Наш композитор nested под sway в окне QEMU
  (vm/run.sh --gui). Vulkan = lavapipe (mesa-vulkan-drivers).
  Порт на stal/ix — после схождения этого кольца (aarch64/lavapipe в ix пока
  бедные, реальные ix-машины — x86_64/radv).
- **Кольцо 2 — KMS в VM**: композитор на VT той же VM: virtio-gpu KMS (atomic) +
  virtio-input + seatd. Окно QEMU показывает наш вывод напрямую; VT-switch и
  hotplug output'ов тестируются тут. Headless CI: vkms(+writeback) или проще —
  свой readback (`vkCmdCopyImageToBuffer` → PNG → golden-assert).
  Проверить в M0: lavapipe экспортирует dmabuf через udmabuf (нужен /dev/udmabuf
  в госте; модификаторы — LINEAR); примет ли virtio-gpu AddFB2 на такой буфер —
  если нет, VM-fallback: dumb buffer + копия (VM-кольцо не про скорость).
  GPU-passthrough (venus) на macOS-хосте не работает — и не нужен.
- **Кольцо 3 — реальное железо (stal/ix, ssh), редко**: только драйверная
  специфика (radv/anv/nvk, реальные модификаторы, латентность, NVIDIA quirks).
  Правила «никогда не ребутать»: запуск только из ssh под супервизором (runit);
  краш процесса сам освобождает DRM master (закрытие fd) — VT возвращается;
  залипший VT — `chvt`/SysRq по ssh; ssh-путь (ethernet) независим от графики.
  Физический ребут — только GPU hang.

Сквозная обвязка: **headless-реализация интерфейса Output** в самом композиторе
(рендер в память + виртуальный seat) — для CI и автономного агентного тестирования;
клиентская матрица: weston-simple-shm / weston-simple-dmabuf-feedback / foot / mpv /
GTK4/Qt6-демо; свой protocol-test-клиент на libwayland-client (сценарии configure
dance / popup grab / clipboard с ассертами); input-инъекция: headless — напрямую
в seat, VM — uinput. wlcs (conformance suite) — опционально позже.

## 13. Референс-код (держать открытым при реализации)

- **kms-vulkan** (nyorain) — минимальный end-to-end «Vulkan рендерит, atomic KMS
  сканирует, семафоры в обе стороны»: https://github.com/nyorain/kms-vulkan (vulkan.c)
- **gamescope** — `src/rendervulkan.cpp` (`CVulkanTexture::BInit` — оба пути:
  allocate-export и import), `src/Backends/DRMBackend.cpp` (`drm_fbid_from_dmabuf`,
  парсинг IN_FORMATS, atomic): https://github.com/ValveSoftware/gamescope
- **wlroots render/vulkan** — только как справочник (не зависимость):
  `vulkan_sync_foreign_texture` / `vulkan_sync_render_buffer` — эталон
  implicit-sync моста; texture.c — импорт dmabuf.
- **kms-quads** (Daniel Stone) — учебный atomic KMS с комментариями:
  https://gitlab.freedesktop.org/daniels/kms-quads
- **QWaylandQuickItem** — модель «окно как виджет» (лок буфера, координаты,
  субповерхности): https://doc.qt.io/qt-6/qwaylandquickitem.html
- Протоколы: https://wayland.app/protocols/ (xdg-shell, linux-dmabuf-v1,
  linux-drm-syncobj-v1, cursor-shape-v1, presentation-time, viewporter)
- Wayland Book: https://wayland-book.com/ (frame callbacks, positioners, subsurfaces)
- dma-buf sync ioctls (ядро ≥6.0): https://www.collabora.com/news-and-blog/blog/2022/06/09/bridging-the-synchronization-gap-on-linux/
- xwayland-satellite: https://github.com/Supreeeme/xwayland-satellite
- Инструмент: drm_info (+ https://drmdb.emersion.fr/ — база поддержки пропертей
  по драйверам)

## 14. Как реализовано (as built, 2026-07)

Статус роадмапа: M1–M3 закрыты, M4 наполовину (попапы/grab'ы, ресайз, рендер по
damage-флагу — есть; clipboard — нет). Firefox-esr работает (рендер, клики, меню).
Код переведён на библиотеку [pg83/std](https://github.com/pg83/std) (namespace
`stl`): ноль зависимостей на C++ STL, владение через `ObjPool` (создание в пуле,
LIFO-смерть), интерфейсы в заголовках / реализации целиком в .cpp, сборка только
clang++ (библиотека использует клэнговые builtins). Кольца 0 и 3 (§12) не
понадобились: весь цикл — кольца 1–2 (QEMU aarch64 + lavapipe, headless и KMS).

### 14.1 Слои и файлы

Зависимости строго вниз: `main` → `control` → {`wayland`, `renderer`} →
{`scene`, `device`, `output`, `input`} → `util`. Один .cpp — один .h (кроме main;
чистые интерфейсы `output.h`/`input_sink.h`/`frame_listener.h` живут без .cpp).

- **Слой 0 — врапперы над ядерными механизмами.**
  - `device.h` — `Device`: один графический адаптер = Vulkan-девайс + (опционально)
    KMS-узел. Владеет DRM fd, VkInstance/VkDevice/очередью и таблицей
    dmabuf-форматов; ev_io на DRM fd (page-flip события) — тоже его. Фабрика
    своего: `createOutput(connector, mode)` и `createRenderer(scene, output,
    frameListener, framesLimit)`. Реализации: `createKms` (путь или nullptr =
    первый узел с atomic) и `createHeadless` (lavapipe, KMS-половины нет).
    Соответствие Vulkan ↔ DRM ищется через `VK_EXT_physical_device_drm`
    (major:minor против fstat); нет совпадения — первый Vulkan-девайс и
    readback-мост (реальность VM: рендер lavapipe, сканаут virtio-gpu — честный
    cross-device). Хендлы уезжают в Renderer внутренним контрактом `DeviceVk`
    (device_vk.h, только между device.cpp и renderer.cpp). `Device::list()` —
    enumeration для `imway --list`: DRM-узлы, коннекторы с режимами (preferred
    помечен `*`), Vulkan-девайсы с их drm-узлами.
  - `output.h` — `Output`: `width/height/refresh()` (режим дисплея; вызывающий
    подгоняет сцену под него), `start()` (modeset + первый чёрный кадр; headless —
    no-op), `present(pixels)`. KmsOutput = коннектор+CRTC+plane+режим
    (2 dumb-буфера; если предыдущий flip ещё в полёте — кадр дропается; VT в
    K_OFF/KD_GRAPHICS), HeadlessOutput = WxH@hz из конфига. Выбор: коннектор по
    имени («HDMI-A-1», nullptr = первый подключённый), режим по «WxH@Hz»
    (nullptr = preferred из EDID).
  - `input.h` — `InputSink`: `motion` (абсолютные координаты output), `button/key`
    (сырые evdev-коды), `scroll` (деления колеса). `InputSource::createLibinput`
    (libinput/udev; outW/outH — границы относительного курсора и масштаб
    абсолютного). `InputSink::tee` размножает поток на два синка. Источники не
    знают, кто потребляет. Input-устройства — отдельная от Device ось: их
    перечисляет udev seat, хотплаг у libinput свой.
- **Слой 1 — `scene.h`: чистые данные, ни одного wayland/vulkan-типа в API.**
  Деревья поверхностей (`Surface` + роли `Subsurface`/`Toplevel`/`Popup`),
  контент (pixels BGRA либо `DmabufBuffer`), применённый viewport, input region,
  view-фидбек. `SurfaceTexture` — непрозрачный указатель, содержимое знает только
  renderer.cpp. Стек субповерхностей: `stackBelow` рисуются до поверхности,
  `stackAbove` — после, оба списка низ→верх. `Toplevel::title/appId` — фиксированные
  буферы: клиенты меняют title постоянно, интернить строки = растить пул.
  `Scene::popups` — порядок создания = порядок стека (последний — самый верхний).
- **Слой 2 — `wayland.{h,cpp}`: вся state machine, единственный владелец
  libwayland и xkbcommon.** Все глобалы и протоколы, commit-семантика, роли.
  Seat — не подсистема, а протокольное состояние ввода внутри SM (`SeatState`:
  фокус/грабы/клавиатура — это реакции протокола на вход). Протокольные части
  модели (pending, sync-кэши, xdg-ресурсы) — приватные impl-наследники структур
  сцены; сцене и рендеру не видны. Наружу: `run()` (цикл до quit; эпилог гасит
  клиентов и display), `sink()` и `frameListener()` — виртуальные аксессоры,
  реализация возвращает `this`; так wayland.h не тянет input.h/renderer.h.
- **Слой 3 — `renderer.{h,cpp}`: view сцены, ImGui+Vulkan, ноль знаний о
  Wayland.** Сам владеет кадровым клоком, сам тянет контент нод по `dirty`,
  сам владеет текстурами. Реализует `InputSink`: ImGui и есть оконный менеджер,
  ему нужен сырой ввод (двигает/ресайзит окна).
- `control.{h,cpp}` — отладочный харнесс поверх публичных API других слоёв:
  FIFO с текстовыми командами `motion X Y | button left|right|middle
  press|release | key CODE press|release | type TEXT | scroll N |
  screenshot PATH | quit`; инъекция ввода через InputSink, скриншот через
  Renderer, quit через ev_break. Внутри — таблица ascii→evdev (us-раскладка).
- `main.cpp` — сборка графа в один `ObjPool`: порядок создания = обратный
  порядок смерти, сцена умирает последней. Клиенты гибнут первыми (в эпилоге
  `run()`), их текстуры уезжают в `orphanedTextures`; подсистемы умирают вместе
  с пулом. Ошибки — исключения `stl::Exception` (`STD_VERIFY`/`Errno().raise`),
  ловятся на границе main.

### 14.2 Контракты между слоями

- **`Surface::dirty`** — SM ставит на commit, renderer снимает и заливает контент
  в текстуру. `dmabuf != nullptr` ⇒ контент в dmabuf (pixels пусты), иначе pixels —
  BGRA, плотные строки w*4.
- **`Scene::orphanedTextures`** — SM складывает текстуры уничтоженных нод,
  renderer освобождает на своём тике (через `vkQueueWaitIdle` — грубо, но
  корректно). Это единственная развязка лайфтаймов SM ↔ renderer.
- **view-фидбек** — renderer пишет в сцену из ImGui-кадра: `imgX/imgY` (экранная
  позиция Image-итема), `hovered`, `desiredW/desiredH` (контент-регион окна).
  SM читает: пик указателя — по hovered, ресайз — configure по desiredW/H
  (с дедупом по последнему отправленному размеру).
- **Кадровый клок — у renderer'а** (ev_timer с периодом 1/hz): тик без
  `needsFrame` — пустой (идеальный idle = 0 кадров; lavapipe — это CPU), после
  активности дорисовываются 3 settle-кадра (hover/анимации ImGui). Полный кадр:
  освободить orphaned → залить dirty-текстуры → ImGui-кадр → `Output::present`
  → `FrameListener::frameShown(msec)`. По нему SM шлёт frame callbacks — всем
  деревьям, показанным в кадре, включая попапы (GTK не рисует контент меню, пока
  не получит frame done) — и configure по view-фидбеку.
- **dmabuf-форматы GPU** — знание Device (`dmabufFormatCount/dmabufFormat`),
  передаются в `WaylandConfig` как данные: SM не зависит ни от девайса, ни от
  рендера. Пустой список = dmabuf-глобал не поднимается. Поскольку форматы
  известны до рождения рендера, граф создаётся за один проход: Scene → Device →
  Output → Wayland → Renderer (FrameListener приходит конструктором,
  сеттеров-дособирателей нет).
- Любой ввод и key press будят кадр (`needsFrame`), даже если ImGui клавиши не
  потребляет.

### 14.3 Осознанные упрощения (расхождения с §1–§8)

- **Scanout — не dmabuf-путь §1**: Vulkan readback → memcpy в dumb-буфер
  (с учётом pitch) → atomic commit. Для VM-кольца достаточно (virtio-gpu,
  скорость не цель); §1 остаётся планом для железа. Соответственно вся
  синхронизация §2 пока не нужна: render target финализируется в TRANSFER_SRC,
  копия в readback каждый кадр дёшева на CPU-девайсе (оттуда же скриншоты).
- **linux-dmabuf v3, не v4** (без feedback): v1 — format-события, v3 —
  modifier-события. Импорт только одноплоскостных буферов;
  ARGB8888/XRGB8888 ↔ `VK_FORMAT_B8G8R8A8_UNORM`, X-вариант получает alpha=1
  свизлом VkImageView. Буфер держится до замены следующим (рендер читает память
  напрямую, копий нет); если клиент уничтожил wl_buffer, пока тот показан, —
  память живёт на нашем fd (импорт дублирует fd, `vkAllocateMemory` забирает
  дубликат себе). Сбой импорта шлёт клиенту failed, но не роняет композитор.
- **Damage** — только как флаг «нужен кадр»: заливка текстуры на commit полная,
  damage-rects не используются.
- **Commit-семантика**: sync-кэши субповерхностей по спеке (commit родителя
  применяет всё sync-поддерево; переход sync→desync применяет накопленный кэш;
  позиция любых детей — в т.ч. desync — двойнобуферизована коммитом родителя).
  Два упрощения: dmabuf-контент применяется сразу даже у sync-детей (буфер один,
  кэшировать нечего), input region применяется сразу (хит-тест не должен ждать
  commit родителя; GTK-оверлеям достаточно). shm-путь снимает копию пикселей
  прямо на commit — wl_buffer сразу возвращается клиенту (важно для
  single-buffered).
- **Input region** — без честной булевой геометрии: subtract выкидывает только
  целиком совпавшие прямоугольники, частичные пересечения игнорируются
  (реальным клиентам хватает).
- **Позиционер**: anchor/gravity полностью, constraint_adjustment
  (флипы/слайды у краёв) — нет: в ImGui-окне попап и так виден. Кроп попапа по
  geometry не делается. `place_above/below` с ref не-sibling'ом — по спеке
  protocol error; прощаем и кладём наверх (ref == сам родитель — валиден).
- **xdg**: первый configure — в ответ на commit без буфера (спека, GTK на этом
  ломается в обе стороны); focus-on-map; начальный размер ImGui-окна — под
  первый буфер, дальше размером владеет пользователь.
- **Инертные заглушки**: `wl_data_device_manager` (клипборд — следующий этап),
  decoration — всегда `server_side` без переговоров. `set_cursor` клиентов
  игнорируется — курсор рисует ImGui (`MouseDrawCursor` на kms-выводе).
  Дефолтный шрифт ImGui — ASCII-only, кириллица в заголовках будет □□□.

### 14.4 Грабли (найдены отладкой — не наступать повторно)

- **KMS/VT**: обязательно `KDSKBMODE K_OFF` + `KDSETMODE KD_GRAPHICS`, иначе
  ввод дублируется в getty под композитором, а курсор консоли мигает поверх кадра.
- **Попапы рисуются в ImGui foreground draw list**, не отдельными ImGui-окнами:
  z-order отдельных окон управляется фокусом и проигрывает toplevel'у. hovered
  для них считается вручную (попапы всегда сверху, перекрыть их некому). Позиция —
  от `imgX/imgY` родителя, записанных этим же кадром, поэтому попапы едут за
  окном бесплатно.
- **press обязан перепикивать цель**: hovered-флаги обновляются кадром позже
  motion; без re-pick клик после одиночного movement уходит мимо клиента.
- **Пик внутри дерева** — последний hovered в порядке отрисовки (поздний Image
  перекрывает ранние; между окнами z-order уже учтён ImGui). Поверхности, чей
  input region мимо точки, прозрачны для ввода.
- **Grab-попапы**: клавиатура уходит попапу (override поверх фокуса toplevel'а),
  клик мимо всех поверхностей клиента закрывает грабы каскадно сверху вниз до
  попавшего; после dismiss клавиатура возвращается toplevel'у. Implicit grab
  указателя — пока зажата хоть одна кнопка, цель залочена. На unmap toplevel'а
  фокус отдаётся последнему замапленному.
- **frame callbacks**: деструктор wl_resource выпиливает callback из списка
  поверхности — список забирается целиком до итерации, иначе итерация по
  мутирующему вектору.
- **Impl-наследники структур сцены не должны шедоуить её поля**: поле `dirty`,
  продублированное в наследнике, молча развело SM (писал в тень) и renderer
  (читал базу) — контент переставал доезжать до экрана.
- Колесо над клиентской поверхностью — клиенту, и не должно скроллить само
  ImGui-окно.
- ImGui: descriptor pool с запасом (512, fonts + AddTexture), `IniFilename =
  nullptr` (не гадить imgui.ini), `HasMouseCursors` — ради курсоров ресайза за
  края окна.
- `stl`-специфика: `struct Output` конфликтует с `stl::Output` при
  `using namespace stl` — в .cpp писать `::Output`; сырые строковые литералы не
  стримятся в sysO/sysE — суффикс `_sv` (util.h), поля-массивы char — через
  каст `(const char*)`.

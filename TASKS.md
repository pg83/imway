# Доклад

`imway` — уже не игрушечный compositor, а вертикально интегрированная Wayland-среда: собственный protocol server, ImGui window manager/shell, Vulkan renderer, DRM/KMS backend, input, DBus, audio, Wi‑Fi, notifications и screenshot workflow. Главный вывод deep dive: protocol/lifetime-часть неожиданно сильная, но session-level recovery и security policy ещё не соответствуют широте функциональности.

Проект очень молодой: 342 коммита, текущая история началась 13 июля 2026 года; около 57.9 тыс. строк C/C++ с тестами. Поэтому качество отдельных подсистем высокое, но архитектурные контракты ещё не везде закреплены.

## Архитектура

Один бинарник работает в трёх режимах через [main.cpp](/home/pg/monorepo/imway/main.cpp:9):

- supervisor — основной entrypoint;
- `composer` — Wayland compositor;
- `screenshot` — отдельный GLFW/Vulkan cropper.

Основной поток:

```text
libinput/libseat ──> InputRouter ──> Renderer/ImGui shell ──> Wayland seat
                                           │
Wayland clients ──> pending/cache/FIFO ──> Scene
                                           │
                                           v
                    direct KMS scanout  или  FP16 linear BT.2020 composition
                                           │
                                      KMS / headless
                                           │
                               pageflip/fence completion
                                           │
                        buffer release + frame callbacks
```

[Composer](/home/pg/monorepo/imway/composer.h:38) — non-owning wiring board. Статические подсистемы живут в корневом `ObjPool`, динамические Wayland-объекты — в `SmallObjAllocator`, GPU/buffer lifetime — в ARC-подобных `FrameResource`. Обратные ссылки защищены собственной weak-ring реализацией в [weak_ptr.h](/home/pg/monorepo/imway/weak_ptr.h:5).

[Scene](/home/pg/monorepo/imway/scene.h:83) — общая mutable-модель поверхностей, toplevel, popup, focus, cursor и constraints. Почти всё однопоточное; отдельный worker используется для тяжёлого screenshot/readback. Это упрощает lifetime, но делает callback latency и любое блокирование event loop критичными.

Wayland реализован непосредственно на libwayland, без wlroots. `wayland.cpp` — монолит примерно на 12.9 тыс. строк с большим набором протоколов: xdg-shell, dmabuf, explicit sync, FIFO/commit timing, capture, color management, DnD, tablet, input method, DRM lease и др.

Renderer использует ImGui как window manager. Композиция идёт в FP16 linear BT.2020, затем применяется output mapping в SDR/HDR, night-light и dithering. Для подходящего fullscreen dmabuf есть direct scanout через [device_kms.cpp](/home/pg/monorepo/imway/device_kms.cpp:2892). Headless использует тот же Vulkan renderer.

Screenshot subsystem особенно хорошо продуман: KMS может передать cropper’у scanout DMA-BUF без копии; fallback делает асинхронный readback в memfd. Cropper сохраняет lossless JXL, сохраняя HDR как BT.2100/PQ, и предоставляет PNG fallback для clipboard.

Ограничения текущей архитектуры:

- фактически один output: один `Scene::outW/outH`, один `Composer::output`;
- сильная связанность через глобальную mutable Scene;
- `wayland.cpp` и `renderer.cpp` уже слишком велики для безопасной параллельной эволюции;
- исключение или долгий callback блокирует весь compositor.

## Где проект уже силён

Выводы агентов в [PLAN.md](/home/pg/monorepo/imway/PLAN.md:13) в этой части подтверждаются.

- SHM доступ корректно обёрнут `wl_shm_buffer_begin_access/end_access`; есть SIGBUS regression test и дополнительная проверка stride.
- Dmabuf validation проверяет plane indices, дубликаты, gaps, modifiers, format plane count и арифметическое переполнение.
- `focusGeneration` защищает serial от replay после смены focus — это сильнее типичной простой serial ring.
- DnD teardown покрыт полноценной kill matrix source/target × enter/motion/drop/finish.
- Damage/viewport/positioner arithmetic систематически выполняется через `i64` и saturating/clamping helpers.
- `wl_surface.commit` действительно транзакционный: pending state, synced subsurface cache, FIFO, explicit sync и frame-resource lifetime согласованы между собой.
- Client dmabuf удерживается до завершения GPU/KMS frame, а release timeline сигналится даже при смерти клиента.
- Тестовый binary использует allocator poisoning `0xAB/0xDE`, live-object balance и frame pointers.
- Protocol error discipline в большинстве request handlers хорошая: ошибка уничтожает клиента, а не compositor.

Это уже сильнее среднего самописного compositor.

## Что в PLAN нужно уточнить

Сам документ качественный, но execution plan требует нескольких поправок.

1. GPU recovery сложнее трёх timeout’ов.

Кроме трёх `vkWaitForFences(UINT64_MAX)` в [renderer.cpp](/home/pg/monorepo/imway/renderer.cpp:927), есть бесконечный wait в [screenshot_capture.cpp](/home/pg/monorepo/imway/screenshot_capture.cpp:164), `vkQueueWaitIdle`, `vkDeviceWaitIdle` и waits внутри ImGui backend. После fence timeout обычный teardown сам может навечно зависнуть на `vkDeviceWaitIdle`.

Минимально надёжный recovery должен после GPU fatal завершать child без Vulkan teardown и делегировать очистку ядру, например специальным `_exit` code. Для зависшего вызова, который вообще не возвращает управление, нужен внешний watchdog supervisor’а.

2. Supervisor пока не готов к restart.

[main_supervisor.cpp](/home/pg/monorepo/imway/main_supervisor.cpp:247) игнорирует `SIGCHLD` с `SA_NOCLDWAIT`, не знает exit status compositor’а, а EOF pipe трактует как успешное завершение и убивает всю process group. Простого цикла вокруг `startComposer()` недостаточно.

3. Explicit-sync eventfd-механики в imway нет.

Утверждение PLAN, что `drmSyncobjEventfd` уже используется в core, неверно: в дереве нет ни одного вызова. Переход с [100 ms CPU wait](/home/pg/monorepo/imway/renderer.cpp:4080) на parking требует нового blocker/transaction слоя и интеграции с CommitCache, FIFO и synced subsurfaces.

4. ANR ping принимает любой pong.

[wmBasePong](/home/pg/monorepo/imway/wayland.cpp:1178) игнорирует переданный serial и просто выставляет `acked=true`. Для UX это почти незаметно, но hostile client легко симулирует liveness произвольными serial. Состояние следует вести per-client, а не per `xdg_wm_base` resource.

5. Frame callback filtering реализован частично.

Сейчас callbacks выдаются только mapped, non-minimized toplevel и mapped popup в [WaylandImpl::onListen](/home/pg/monorepo/imway/wayland.cpp:12632). Это уже лучше, чем утверждает gap list. Но occlusion не учитывается, а callbacks рекурсивно выдаются всем subsurface видимого root независимо от фактического участия в кадре.

6. Security filter — denylist.

[privilegedGlobal](/home/pg/monorepo/imway/wayland.cpp:6263) скрывает восемь интерфейсов только от security-context-tagged клиентов. Обычный same-UID клиент видит capture, synthetic input и другие чувствительные протоколы согласно denylist. DRM lease в список не входит. Для hostile-client threat model нужен allowlist либо runtime permission layer, а не только Flatpak-oriented фильтр.

7. OOM policy не полностью безопасна.

`wl_resource_create` почти всегда проверяется, но последующий `alloc->make<T>` в 52 местах использует infallible allocator. При исчерпании памяти libstd приходит к `STD_INSIST`, а не `post_no_memory`. Правило из PLAN «client-sized allocation never throws/aborts» пока не обеспечено самим allocator API.

## Приоритетный план работ

### 1. Dmabuf actual-size validation — S

Добавить в `paramsMakeBuffer` проверку через `lseek(fd, 0, SEEK_END)`:

- `offset <= size`;
- `offset + stride <= size` для каждого plane;
- `offset + stride * height <= size` только для plane 0;
- non-seekable fd пропускать;
- сохранить различие `create`/`create_immed`.

Готовность: отдельные tests на short fd, overflow, non-seekable fd и multi-plane format. Это небольшой независимый P0 и лучший первый merge.

### 2. Переписать supervisor как state machine — L

До GPU recovery:

- перестать использовать `SA_NOCLDWAIT` для compositor;
- хранить composer PID и реальный exit status;
- отдельно reap’ить spawned apps;
- создавать новый control pipe на каждый restart;
- clean exit завершает session, специальный GPU-fatal code перезапускает;
- retry budget, например 3 рестарта за 30 секунд, с backoff;
- после исчерпания бюджета возвращать nonzero, а не скрывать crash как success;
- добавить heartbeat/watchdog, способный `SIGKILL` зависший composer.

Готовность: тесты на clean exit, crash, restart, crash loop и зависший heartbeat.

### 3. Единый GPU-fatal contract — L

После supervisor:

- централизовать классификацию `VkResult`;
- убрать `abort()` из [imguiVkCheck](/home/pg/monorepo/imway/renderer.cpp:130);
- запретить исключения, уходящие через libev/libwayland C callback boundary;
- заменить compositor-side infinite fence waits bounded helper’ом;
- включить screenshot capture, cursor rasterization и readback;
- не выполнять `vkDeviceWaitIdle` после timeout/device-lost;
- runtime `VK_CHECK`, включая client icon texture, преобразовать в локальный frame failure либо GPU-fatal;
- добавить fault injection для `DEVICE_LOST`, `VK_TIMEOUT` и submit failure.

Полная Vulkan device recreation пока не нужна: special exit + bounded supervisor restart даст большую часть практической устойчивости.

### 4. Resource ceilings и fallible allocations — M/L

- `wl_display_set_default_max_buffer_size(1 MiB)`;
- сохранить `maxImageDimension2D` в `DeviceVk`;
- проверять SHM размер до выделения `Surface::pixels`, а dmabuf — до `vkCreateImage`;
- вести один callback counter на surface, включая pending/cache/FIFO queues;
- определить корректную политику превышения callback cap;
- добавить `SmallObjAllocator::tryMake` и перевести client-created wrapper objects на `post_no_memory`.

### 5. Permission model — M/L

- составить явную allowlist core-протоколов для sandbox;
- добавить DRM lease и security-context manager в аудит;
- capture, virtual keyboard, input method, data-control и DRM lease перевести на `DENY/ASK/ALLOW`;
- по умолчанию требовать подтверждение и для несандбоксированного неизвестного клиента;
- добавить registry snapshot tests для sandboxed/untrusted/trusted clients.

### 6. ANR — M

- per-client `pendingSerial`, missed-pong counter и credentials;
- принимать только точный pong serial;
- `Toplevel::unresponsive`, tint/titlebar state;
- Close на unresponsive окне открывает Wait/Terminate;
- для kill лучше pidfd, чтобы исключить PID reuse;
- SIGTERM → короткий grace period → SIGKILL;
- тесты на wrong pong, frozen client, recovery after pong и client death during dialog.

### 7. Explicit-sync commit blocker — L

Не делать это как renderer patch. Нужна самостоятельная транзакционная задача:

- `drmSyncobjEventfd` + libev watcher;
- commit остаётся cached до acquire signal;
- никогда не превращать timeout в protocol error;
- корректно release при destroy/superseding commit;
- сохранить ordering относительно FIFO и synced subsurfaces;
- проверить, что один never-signalling client не тормозит второй.

### 8. P2 после стабилизации

- direct-scanout result сделать tri-state: accepted/transient/rejected; taint только permanent rejection;
- передавать в Wayland множество реально представленных surfaces и выдавать callbacks/presentation feedback только им;
- ASAN/UBSAN build target и CI;
- постоянная категория `dont_crash_*`;
- tests «client dies mid resize/popup grab»;
- deferred global destruction отложить до настоящего dynamic output/hotplug;
- после покрытия тестами разделить `wayland.cpp` по protocol clusters. Рефакторить монолит до P0 fixes не стоит.

С разделом PLAN «explicitly do not do» согласен: не вводить protocol timeout для explicit-sync acquire, не ставить произвольные caps на surface/region count и не отключать клиента за косметическую ошибку cursor scale.

## Проверка состояния

Исходное дерево осталось чистым. Сборка через `~/monorepo/ix/ix run set/pg/libs -- ./build` прошла: 702 compile/link узла без ошибок; наблюдался только шум `-Wunused-command-line-argument` для `-pthread`.

Сохранённый полный verdict от 22 июля: [339 ok, 6 skip, 1 xfail](/home/pg/monorepo/imway/.build/cas/54/5473a3ddf635f290a82474ed52161d301beadfbe347268039e4c58c8ace6fab6:1). Единственный ожидаемый fail — fractional HiDPI: preferred scale пока жёстко равен 120. В текущем опубликованном `.build/test-results/verdict.txt` находится filtered-прогон `cache_frames_uaf` — остановленная выборочная команда успела завершить этот один сценарий. Полный suite повторно не запускался по вашей команде.

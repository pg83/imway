# План усиления тестов Wayland

## Цель

Проверить не только нормальную работу compositor, но и всю границу доверия:

- обязательные protocol errors и точный object/interface, на котором они возникают;
- неправильный порядок запросов и повторное использование одноразовых объектов;
- уничтожение связанных ресурсов в неудобном порядке;
- внезапная смерть клиента в промежуточном состоянии;
- плохие fd, размеры, offsets, strides, serials и enum values;
- ограниченные по размеру стресс-нагрузки, способные выявить переполнение, рекурсию,
  квадратичную работу и утечки;
- восстановление compositor после отключения злонамеренного клиента.

## Организация тестов

1. Один `headless_*.sh` — один логический сценарий.
2. Несвязанные ошибки никогда не проверяются последовательным циклом в одном тесте.
3. Для каждого сценария создаётся отдельный compositor: это даёт изоляцию и
   параллельный запуск через `dev/test.py`.
4. Общий код допустим только в небольших `*.inc`/`*.sh` helpers. Сами клиенты должны
   оставаться самостоятельными и легко читаемыми.
5. Negative client обязан проверить:
   - соединение завершилось с `EPROTO`;
   - interface ошибки совпадает с ожидаемым;
   - error code совпадает с ожидаемым.
6. Scenario после клиента обязан проверить, что compositor жив. Для transport и
   lifetime атак дополнительно запускается здоровый canary client, который создаёт
   toplevel, коммитит buffer и получает frame callback.
7. `SKIP` разрешён только при реальном отсутствии необязательной возможности:
   explicit sync, render node, `/dev/udmabuf`. Core/xdg/data-device тесты не должны
   иметь skip-path.
8. Локальный musl-хост запускает обычную сборку. ASan/UBSan запускаются отдельно на
   glibc-хосте; тесты не должны зависеть от наличия sanitizer runtime.

## Уже сделано

### Core, xdg-shell, subsurface и viewporter

Коммит `74a98b5` добавляет отдельные сценарии для:

- invalid `buffer_scale`;
- commit `xdg_surface` без role;
- buffer до initial configure/ack;
- повторного создания xdg role;
- уничтожения `xdg_surface` раньше role object;
- неправильной window geometry;
- buffer до `get_xdg_surface`;
- invalid positioner size, anchor, gravity, constraints и parent size;
- уничтожения не верхнего popup;
- неправильного nested popup grab;
- повторной subsurface role;
- restack относительно не-sibling;
- fractional viewport source без destination;
- использования viewport после смерти surface.

### linux-dmabuf validation

Коммит `0494cb6` добавляет отдельные сценарии для:

- plane index вне диапазона;
- повторного plane;
- нулевого stride;
- отсутствующих и лишних planes;
- неправильных dimensions;
- неизвестного format;
- разных modifiers;
- `offset + stride * height` overflow;
- неподдерживаемых flags;
- fd, который не является dmabuf;
- повторного использования params;
- asynchronous `failed` для обычного `create`.

Вместе с тестами исправлены overflow check, проверка количества planes, flags и
ранняя проверка импортируемости fd.

### Input-related state errors

Коммит `3dee457` добавляет отдельные сценарии для:

- повторного использования activation token;
- incomplete color description;
- duplicate pointer constraint;
- duplicate keyboard-shortcuts inhibitor;
- cursor surface с уже занятой role;
- повторного selection source;
- DnD invalid action mask;
- неправильного preferred action;
- недоступного preferred action;
- преждевременного `finish`;
- повторного drag source;
- drag icon с занятой role;
- offer после уничтожения source.

Вся тематическая пачка повторно собрана и входит в полный suite.

### Аудит последующих пачек

- `18c1381`: explicit-sync errors и lifetime;
- `5fec611`: wire transport corruption, fd cleanup и health probe;
- `073b98d`: hostile buffer lifetime;
- `f5f7180`: arithmetic boundaries и насыщение attach offsets;
- `6f69238`: depth/region/resource limits, growable Vulkan descriptor pools и
  исправления переполнений строковых scratch buffers;
- текущая data-device пачка: post-finish state errors, drag/selection source
  isolation, serial scope, teardown и SIGKILL source/target во всех фазах;
- текущий version/serial аудит: `wl_output` v1/v2/v4, `wl_seat` v1/v2/v5 и
  запрет replay старых selection/activation serial после focus transfer.

## Explicit sync (сделано: `18c1381`)

Каждый пункт — отдельный test/scenario. При отсутствии
`wp_linux_drm_syncobj_manager_v1` сценарий делает `SKIP`.

1. `invalid_timeline`
   - передать обычный memfd вместо syncobj fd;
   - ожидать `MANAGER_ERROR_INVALID_TIMELINE`.
2. `duplicate_surface`
   - дважды получить sync surface для одного `wl_surface`;
   - ожидать `MANAGER_ERROR_SURFACE_EXISTS`.
3. `dead_surface`
   - создать sync surface, уничтожить исходный `wl_surface`;
   - затем установить point;
   - ожидать `SURFACE_ERROR_NO_SURFACE`.
4. `no_buffer`
   - установить acquire point без нового attached buffer;
   - commit;
   - ожидать `SURFACE_ERROR_NO_BUFFER`.
5. `no_acquire`
   - attach buffer без acquire point;
   - ожидать `SURFACE_ERROR_NO_ACQUIRE_POINT`.
6. `no_release`
   - attach buffer и установить только acquire point;
   - ожидать `SURFACE_ERROR_NO_RELEASE_POINT`.
7. `unsupported_buffer`
   - установить оба points и attach `wl_shm` buffer;
   - ожидать `SURFACE_ERROR_UNSUPPORTED_BUFFER`.
8. `conflicting_points`
   - attach настоящий dmabuf;
   - acquire и release используют один timeline, release не больше acquire;
   - ожидать `SURFACE_ERROR_CONFLICTING_POINTS`;
   - допускается `SKIP`, если `/dev/udmabuf` отсутствует.
9. `timeline_destroyed_after_set`
   - установить pending points, уничтожить protocol timeline objects, commit;
   - compositor должен удерживать внутренние refs до завершения commit/frame.
10. `surface_destroyed_with_pending_points`
    - установить points и уничтожить surface до commit;
    - закрытие клиента и compositor должны завершиться чисто.

## Transport и повреждённые Wayland messages (сделано: `5fec611`)

Каждый malformed message запускается отдельным тестом. После отключения клиента
обязательно запускается canary client.

1. Неизвестный object id.
2. Неизвестный opcode у существующего object.
3. Message size меньше минимального header.
4. Message size не выровнен на четыре байта.
5. Объявленный длинный message, после которого клиент закрывает socket посередине.
6. Повторное использование занятого `new_id`.
7. Лишний `SCM_RIGHTS` fd перед последующей ошибкой протокола.
8. Отсутствующий fd для request, который его требует.
9. Несколько fd при request с одним fd.
10. Серия из 64–256 клиентов с ошибочным ancillary fd:
    - сравнить число `/proc/$IMWAY_PID/fd` до и после;
    - допустим только небольшой фиксированный шум, не линейный рост.
11. Отключение после создания registry, surface и callback, но до полного request.
12. Отключение сразу после flush большого числа корректных requests.

## Buffer lifetime (сделано: `073b98d`)

Отдельные сценарии:

1. Attach buffer, уничтожить `wl_buffer` до commit.
2. Attach buffer, заменить другим buffer, уничтожить оба в обратном порядке.
3. Один shm buffer одновременно используется двумя surfaces; уничтожается после
   commit обеих.
4. Sync subsurface кеширует buffer, protocol buffer уничтожается до parent commit.
5. Sync subsurface кеширует frame callback, затем по очереди умирают surface,
   subsurface и parent.
6. Presentation feedback уничтожается клиентом до frame.
7. Surface уничтожается с pending frame callbacks и presentation feedback.
8. Shm pool уничтожается сразу после создания buffer.
9. Shm pool уменьшается до нуля после первого успешного frame и buffer коммитится
   повторно.
10. Несколько последовательных resize/recreate циклов с уничтожением старого buffer
    до показа нового.
11. Dmabuf protocol object уничтожается сразу после commit, storage должен жить до
    release последнего GPU/frame use.
12. Один dmabuf используется parent и sync subsurface одновременно.

## Surface arithmetic и размеры (сделано: `f5f7180`)

1. `wl_surface.damage` с `INT32_MIN/MAX` по всем четырём аргументам.
2. `damage_buffer` с теми же крайними значениями.
3. Повторные `attach` с большими положительными и отрицательными offsets.
   Проверять отсутствие signed overflow под UBSan на отдельном хосте.
4. Buffer dimensions около границ делимости `buffer_scale` после всех transforms.
5. Viewport source у правой/нижней границы buffer с минимальной fractional частью.
6. Source rectangle, где сложение `x + width` близко к пределу fixed-point.
7. Window geometry с крайними coordinates и маленьким положительным размером.
8. Positioner anchor rect и offsets около `INT32_MIN/MAX` для всех flip/slide/resize
   constraint combinations.
9. Shm stride ровно `width * 4`, на единицу меньше и максимально большой,
   разрешённый pool.
10. Большой, но ограниченный buffer должен либо корректно обрабатываться, либо
    детерминированно отвергаться; compositor не должен падать от allocation failure.

## Глубина и алгоритмическая нагрузка (сделано: `6f69238`)

Эти тесты не должны пытаться съесть всю память машины. Используются фиксированные
разумные пределы и timeout.

1. Цепочка sync subsurfaces глубиной 64, 256, 1024:
   - commit снизу вверх;
   - frame callback на самом глубоком surface;
   - уничтожение дерева сверху вниз и снизу вверх.
2. Та же цепочка в desync mode.
3. Широкое дерево: один parent и 1024 siblings, затем restack каждого.
4. Region fragmentation:
   - один большой rectangle;
   - серия вертикальных и горизонтальных subtract;
   - set как input/constraint region;
   - проверить ограниченное время ответа и отсутствие неограниченного RSS growth.
5. Повторное копирование fragmented region между pending/cache/current state.
6. Popup chain разумной глубины с уничтожением parent раньше child; protocol error
   не должен оставлять grab/focus state.
7. Тысячи frame callbacks на одном surface, затем уничтожение surface до frame.
8. Тысячи короткоживущих surfaces с полным create/commit/destroy циклом.

Если текущая реализация не выдерживает выбранный предел, сначала вводится явный
лимит или итеративный обход, затем тест фиксирует новый контракт.

## Resource limits и крупные payloads (сделано: `6f69238`)

1. Toplevel icon:
   - слишком большой квадратный shm buffer;
   - много buffers в одном icon object;
   - один buffer добавлен несколько раз;
   - buffer уничтожен при нескольких watches;
   - размер/число должны иметь явный лимит или безопасный отказ.
2. Очень длинные title, app_id, icon name и MIME strings на границе Wayland message.
3. Более 64 MIME offers: текущий cap должен работать без изменения старых entries.
4. Много activation token requests: grant queue остаётся ограниченной 64 entries,
   старые token не активируют surface.
5. Большое число idle notifications/inhibitors с последующим disconnect клиента.
6. Большое число pointer gesture/relative-pointer objects и их массовое уничтожение.
7. Fd-bearing requests в цикле с проверкой `/proc/$pid/fd` после каждой группы.

## Data-device state machine (сделано в текущей пачке)

Дополнить отдельными тестами:

1. `accept` после успешного `finish` -> `INVALID_OFFER`.
2. `receive` после `finish` -> `INVALID_OFFER`, переданный fd закрывается.
3. Повторный `finish` -> `INVALID_FINISH` или `INVALID_OFFER` согласно текущей
   версии протокола.
4. `set_actions` у selection offer -> `INVALID_OFFER`.
5. `set_actions` после смерти source -> `INVALID_OFFER`.
6. Drag source пытаются использовать как selection -> `INVALID_SOURCE`.
7. Selection source пытаются использовать как drag -> `USED_SOURCE`.
8. Start drag со stale serial: source получает cancelled, compositor state не меняется.
9. Start drag с serial другого surface/client: запрос игнорируется/cancelled.
10. Source, target, offer и icon уничтожаются по одному в каждой фазе drag.
11. Source/target client получает SIGKILL во время enter, motion, drop и finish.
12. После каждого сценария следующий здоровый клиент должен получать pointer и
    keyboard events.

## Serial и focus isolation (частично сделано)

1. Pointer enter serial нельзя использовать другим pointer binding того же клиента.
2. Cursor serial нельзя использовать после leave/re-enter.
3. Button serial одного surface нельзя использовать для move/resize другого.
4. Serial одного клиента нельзя использовать другим клиентом.
5. Keyboard serial нельзя использовать после focus transfer.
6. Popup grab с serial обычной кнопки, key serial, stale serial и чужим serial.
7. Selection serial после переключения focus.
8. Activation token с surface другого клиента и с уничтоженным surface.
9. Повторное использование уже активированного token ничего не меняет.

## Version negotiation (частично сделано)

Для versioned core interfaces нужны отдельные клиенты, bindящие минимальную и
максимальную поддерживаемую версию:

1. `wl_output` v1/v2/v4: не отправлять events новее negotiated version.
2. `wl_seat` старые версии: release/repeat/name gating.
3. `wl_data_device_manager` v1 и v3: cancelled должен приходить v1, новые DnD events
   только v3.
4. `xdg_wm_base` v1/v3: reposition requests/events только при доступной версии.
5. linux-dmabuf v3/v4: modifier events и feedback semantics.
6. Pointer gestures v1/v3: hold gesture только в версии, где он существует.

## Проверка восстановления compositor (сделано)

Добавить `client_health_probe` без собственного scenario:

1. connect и bind core globals;
2. создать небольшой xdg toplevel;
3. дождаться configure и ack;
4. attach shm buffer;
5. получить frame callback;
6. корректно уничтожить protocol objects и disconnect.

Он запускается после transport corruption, fd cleanup, массового resource teardown и
смерти клиента во время grab/drag. Простого `kill -0` недостаточно: процесс может
быть жив, но event loop, focus или renderer уже повреждены.

## Отдельный ASan/UBSan-хост

На glibc-хосте:

1. Собрать compositor с существующей `IMWAY_SANITIZERS=ON`.
2. Обычные test clients можно брать из bootstrap build.
3. Запускать последовательно, чтобы уменьшить шум и память:

   ```text
   dev/test.py --imway build-sanitize/imway --bindir build-boot --jobs 1 --runs 1
   ```

4. Environment:
   - `ASAN_OPTIONS=abort_on_error=1:detect_leaks=1`;
   - `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
5. Отдельно повторять arithmetic, deep-tree, buffer lifetime, raw transport и fd
   suites с `--runs 3`.
6. Любой sanitizer report считается fail даже если scenario успел вернуть 0.

## Порядок реализации

1. [x] Завершить и проверить explicit-sync пачку.
2. [x] Завершить transport/canary/fd cleanup пачку.
3. [x] Добавить buffer lifetime scenarios.
4. [x] Добавить arithmetic boundary scenarios и исправить signed overflow.
5. [x] Добавить depth/region/icon resource tests; определить и зафиксировать лимиты.
6. [x] Дополнить data-device state machine.
7. [ ] Завершить serial isolation и version negotiation (output/seat и stale
   focus serial уже покрыты; остаются остальные interface/version pairs).
8. [x] Прогнать полный обычный suite на musl-хосте (`236 ok`, `0 fail`,
   необязательные возможности дали `6 skip`, известный HiDPI case — `1 xfail`).
9. Прогнать полный sanitizer suite на отдельном glibc-хосте.

Каждая тематическая пачка коммитится только после собственного filtered run. После
последней пачки обязателен полный `dev/test.py --runs 1`; затем отдельный повторный
прогон новых hostile tests с `--runs 3`.

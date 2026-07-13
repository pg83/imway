# imway

Wayland-композитор поверх Dear ImGui: окна клиентов живут внутри ImGui-окон
(текстуры через `ImGui::Image`), весь хром — меню, панели, декорации — рисует
ImGui.

## Стек

Сырой, без фреймворков (никаких wlroots/Smithay):

- **libwayland-server** — протоколы реализованы вручную
- **Vulkan** — единственный рендер-путь, ни одного вызова GL/EGL
  (headless offscreen + readback; ImGui через `imgui_impl_vulkan`)
- **libdrm** — atomic KMS + dumb buffers для вывода на экран
- **libinput + xkbcommon** — ввод
- **libev** — event loop

## Что уже работает

- wl_compositor, wl_shm, wl_subcompositor (sync/desync, z-порядок),
  wl_seat v5 (клавиатура + мышь), wl_output, xdg-shell (toplevel),
  xdg-decoration (server-side), wp_viewporter,
  zwp_linux_dmabuf_v1 v3 (импорт dmabuf в VkImage без копий)
- Бэкенды: `headless` (скриншоты, инъекция ввода через FIFO `--control`)
  и `kms` (atomic modeset, page flip, libinput)
- foot + mc внутри ImGui-окон, интерактив мышью/клавиатурой

## Разработка

Код собирается и тестируется в QEMU-VM (Debian, aarch64+hvf на macOS):

```sh
vm/create.sh   # одноразово: скачать образ, cloud-init с тулчейном
./build.sh     # rsync исходников в VM + сборка + ctest
vm/gui.sh      # окно QEMU с imway на KMS + foot (мышь/клава работают)
```

На Linux `./build.sh` собирает нативно. Тесты — headless-скриншоты с
проверкой пикселей: shm, субповерхности, viewporter, dmabuf (через
udmabuf), клавиатурный e2e (набор команды в foot).

Дизайн и роадмап: [docs/DESIGN.md](docs/DESIGN.md).

Целевая платформа — [stal/ix](https://stal-ix.github.io/): полностью
статическая линковка, Vulkan-драйвер влинкован в бинарь.

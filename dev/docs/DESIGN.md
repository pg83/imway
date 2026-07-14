# imway — a Wayland compositor on top of Dear ImGui

Design document. Ecosystem state — mid-2026.
§0–13 were written before any code; §14 describes what was actually implemented and how.
On any discrepancy, trust §14 — the older sections remain the plan for future rings.

## 0. Foundation (fixed)

- Wayland client windows are embedded into ImGui windows (client texture = `ImGui::Image()`),
  all chrome (menus, panels, decorations) is ImGui. Docking gives us tiling for free.
- **Stack: libwayland-server + libdrm (atomic KMS) + libinput + libev + xkbcommon.**
  No wlroots/Louvre/Smithay — we implement all protocols ourselves.
  SDL3 — only for the nested dev mode, if it is ever needed at all.
- **Vulkan-only.** Not a single OpenGL/EGL/GLES call in our code. ImGui via
  `imgui_impl_vulkan` (`ImTextureID` = `VkDescriptorSet`). Bonus of the Vulkan path: all
  synchronization is explicit — semaphore/sync_file/syncobj, none of the driver's
  implicit-sync magic that on GL "just works, except on NVIDIA".
- There is no direct precedent for "wayland windows inside ImGui" — the idea is new, but
  every building block is proven: QtWayland `QWaylandQuickItem` (windows as toolkit
  widgets, our model is a direct copy), gamescope (Vulkan compositor with ImGui overlays,
  raw atomic KMS), kms-vulkan (a minimal "Vulkan renders, KMS scans out" example).

## 1. Vulkan → KMS: initialization (the main open question — resolved)

Key fact: **GBM is not required**. There are two architectures; both boil down to
"render into a VkImage known to KMS as a dmabuf FB, present with an atomic commit".

### 1.1 Architecture (a): Vulkan allocates, KMS scans out — the gamescope path. OUR CHOICE

1. **Modifier negotiation**: intersection of two sets —
   - KMS: parse the `IN_FORMATS` blob of the primary plane (gate: `DRM_CAP_ADDFB2_MODIFIERS`);
   - Vulkan: `vkGetPhysicalDeviceFormatProperties2` + `VkDrmFormatModifierPropertiesListEXT`
     (e.g. `DRM_FORMAT_XRGB8888` ↔ `VK_FORMAT_B8G8R8A8_UNORM`), confirm each candidate
     with `vkGetPhysicalDeviceImageFormatProperties2` with
     `VkPhysicalDeviceImageDrmFormatModifierInfoEXT` + require
     `VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT`.
2. **Creation**: `vkCreateImage`, `tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT`,
   pNext: `VkImageDrmFormatModifierListCreateInfoEXT{intersection}` +
   `VkExternalMemoryImageCreateInfo{DMA_BUF_BIT_EXT}`. Vulkan has no "scanout" flag —
   scanout suitability is expressed solely by the modifier (sufficient on desktop
   drivers; ARM SoCs with contiguity requirements — see 1.2).
3. **Allocation**: dedicated (`VkMemoryDedicatedAllocateInfo`) +
   `VkExportMemoryAllocateInfo{DMA_BUF_BIT_EXT}`.
4. **Export**: `vkGetMemoryFdKHR` → dmabuf fd;
   `vkGetImageDrmFormatModifierPropertiesEXT` → which modifier the driver picked;
   per-plane layout: `vkGetImageSubresourceLayout(VK_IMAGE_ASPECT_MEMORY_PLANE_i_BIT_EXT)`
   → offset/rowPitch (never compute pitches by hand).
5. **KMS**: on the primary node `drmPrimeFDToHandle` → GEM handles →
   `drmModeAddFB2WithModifiers(…, DRM_MODE_FB_MODIFIERS)` → atomic commit `FB_ID`.
   Careful: GEM handles are not refcounted by the kernel (re-importing the same buffer
   yields the same handle) — a classic source of silent bugs.

2–3 such flippable images per output. Keep a LINEAR fallback (planes without
modifiers, the cursor plane, cross-GPU).

### 1.2 Architecture (b): GBM allocates, Vulkan imports — the wlroots/kms-vulkan path

`gbm_bo_create_with_modifiers2(SCANOUT|RENDERING)` → fd/offset/stride/modifier →
import into Vulkan (`VkImageDrmFormatModifierExplicitCreateInfoEXT` +
`VkImportMemoryFdInfoKHR`). Why: `GBM_BO_USE_SCANOUT` knows vendor placement
constraints (contiguity on SoCs) that Vulkan cannot express — this is exactly why
wlroots kept GBM as the default allocator.

**Decision**: (a) behind a thin allocator interface (~4 functions), so that (b) can be
swapped in with ~150 lines if we hit a hardware wall. GBM drops out of the dependencies.

### 1.3 Device selection
`VK_EXT_physical_device_drm`: `VkPhysicalDeviceDrmPropertiesEXT{primary/renderMajor/Minor}`
checked against `fstat(kms_fd)` → major/minor. Require this extension unconditionally (like
gamescope). The "render device ≠ display device" case (Asahi et al.) — keep in mind, don't solve in v1.

### 1.4 What NOT to use
`VK_KHR_display` / `vkAcquireDrmDisplayEXT` — hides CRTC/planes/properties behind an opaque
swapchain: no atomic properties, no cursor plane, no fences, no VRR. For VR/kiosk, not
for a compositor. Every project we studied agrees.

## 2. Synchronization (all explicit)

- **Frame → KMS**: the submit signals (i) an internal timeline semaphore (one point per
  frame — it also tracks buffer/texture lifetimes instead of VkFence) and (ii) a binary
  semaphore with `VkExportSemaphoreCreateInfo{SYNC_FD}` → `vkGetSemaphoreFdKHR` → sync_file →
  the plane's **`IN_FENCE_FD`** in the atomic commit (`NONBLOCK`). Caution: exporting SYNC_FD
  resets the binary semaphore — this is a feature, wlroots relies on it.
- **Output-buffer reuse**: via the page-flip event (simple) or `OUT_FENCE_PTR` →
  `vkImportSemaphoreFdKHR(TEMPORARY)` (clean; this is what kms-vulkan does).
- **Client implicit sync** (the default for wayland): bridge via dma-buf ioctls (kernel ≥6.0):
  before sampling `DMA_BUF_IOCTL_EXPORT_SYNC_FILE` → temporary import into a binary
  semaphore → wait; after compositing, our render-finished sync_file →
  `DMA_BUF_IOCTL_IMPORT_SYNC_FILE` into the client's buffer (its next write will wait for our
  read) — and this same event = the moment of `wl_buffer.release`. The recipe is verbatim =
  `vulkan_sync_foreign_texture()` / `vulkan_sync_render_buffer()` from wlroots.
- **`wp_linux_drm_syncobj_v1`** (explicit-sync clients; NVIDIA is painful without it):
  gate on `DRM_CAP_SYNCOBJ_TIMELINE`; the acquire point may not be materialized at commit
  time — don't block, wait via `DRM_IOCTL_SYNCOBJ_EVENTFD` in the event loop;
  GPU-wait portably: `drmSyncobjTransfer` → `drmSyncobjExportSyncFile` → import
  the SYNC_FD; release: our sync_file → `drmSyncobjImportSyncFile` → transfer into the
  client's timeline at the release point.

## 3. Client buffers → ImGui

- **wl_shm** (mandatory, first): mmap the pool; staging `VkBuffer` (host-visible) →
  `vkCmdCopyBufferToImage` over the damage rectangles (`wl_surface.damage_buffer`) →
  barriers `TRANSFER_DST → SHADER_READ_ONLY`. `wl_buffer.release` right after the copy is
  recorded into the command buffer and it completes (important for single-buffered clients).
  SIGBUS protection against a client that truncated the fd. `nonCoherentAtomSize` on flush.
- **linux-dmabuf-v1 v4+** (GPU clients; Mesa 25.2 removed wl_drm — without dmabuf
  GPU clients don't work at all): feedback (format table in a sealed memfd,
  `main_device` = the **render node**, tranches); import: `VkImageDrmFormatModifierExplicitCreateInfoEXT`
  + `VkImportMemoryFdInfoKHR` → `VkImageView`. Cache the VkImage per `wl_buffer`
  (clients cycle through 2–4 buffers). Release a dmabuf buffer only once the timeline
  point of the last frame that sampled it has passed.
- **ImGui**: `ImGui_ImplVulkan_AddTexture(view, layout)` → `VkDescriptorSet` =
  `ImTextureID`. Pool with `FREE_DESCRIPTOR_SET_BIT`, sized generously (~4096) — pool
  exhaustion fails non-deterministically across drivers. `RemoveTexture`/destroy view —
  only after the frame retires (timeline point). Backend redesign of 2026-04: separate
  SAMPLED_IMAGE + SAMPLER descriptors, `AddTexture` without a sampler parameter —
  pin the ImGui version and read the changelog on upgrades.
- **YUV (NV12 from video players)**: `VkSamplerYcbcrConversion` requires an immutable
  sampler in the layout — stock imgui_impl_vulkan can't do this (and with separate
  descriptors it fundamentally can't). Solution: our own small blit pass
  YUV→RGBA (immutable-ycbcr-sampler pipeline) before ImGui; ImGui sees plain RGBA.
  Everyone does it this way. RGB dmabufs (the overwhelming majority) work directly.
- Layout transitions of imported images are on us, including manual
  `VK_QUEUE_FAMILY_FOREIGN_EXT` barriers (requires `VK_EXT_queue_family_foreign`).

Extensions (device): `VK_EXT_image_drm_format_modifier`, `VK_KHR_external_memory_fd`,
`VK_EXT_external_memory_dma_buf`, `VK_EXT_queue_family_foreign`,
`VK_KHR_external_semaphore_fd`, `VK_EXT_physical_device_drm`, timeline semaphores (1.2).

## 4. Event loop (libev)

One thread. `wl_event_loop_get_fd()` is libwayland's epoll fd (a single point):

- `ev_io` on the wayland fd → `wl_event_loop_dispatch(loop, 0)`;
- `ev_io` on the libinput fd → `libinput_dispatch` + event fan-out;
- `ev_io` on the DRM fd → `drmHandleEvent` (page_flip_handler2, v3 — per-CRTC) —
  this is the frame clock of each output;
- `ev_prepare` (before sleeping): **`wl_display_flush_clients()`** — a libwayland
  invariant: never go to sleep with unflushed client buffers (otherwise the classic deadlock);
- `ev_timer` — frame deadline / animations / ping timeouts;
- syncobj eventfds (acquire points) — also `ev_io`.

Rendering is on demand: draw a frame if (commit with damage) ∨ (input) ∨ (ImGui
animation) ∨ (frame callbacks are owed). Perfect idle = 0 fps. ImGui regenerates all
geometry every frame — per-pixel output damage is not for us
(`LOAD_OP_DONT_CARE`, full redraw), but client-side damage is mandatory: for
shm uploads and for deciding "should we draw a frame at all".

**Frame callbacks — the contract**: send `wl_surface.frame done()` on page-flip only
to surfaces actually shown in the frame. Invisible ones get nothing (that's throttling),
visible ones must get it (otherwise the client freezes). Too early (right on commit) —
clients spin at 1000+ fps.

## 5. Input

### 5.1 Two consumers
Every event is forked: ImGui (translated ImGuiKey, cursor position) and clients
(**raw evdev codes** + surface-local coordinates). libinput provides evdev codes and
acceleration out of the box. xkbcommon on our side is for ImGui text (`xkb_state_key_get_utf8`)
and hotkeys; clients get a keymap fd (memfd, sealed) + `wl_keyboard.modifiers` after
every enter and on serialization changes. Key repeat: for ImGui we do it ourselves
(hardware has none), clients repeat on their own per `repeat_info(25, 600)` — we do NOT
repeat for them. Don't forget keyboard LEDs (`libinput_device_led_update`).

Nested/SDL3: `SDL_KeyboardEvent.raw` (3.2+) — the evdev code on the wayland backend (on X11
it's an X keycode = evdev+8, normalize); filter `event.key.repeat`; grab the parent's
keymap with our own bind of wl_seat via `SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER`.

### 5.2 Focus routing (this is where every "input is broken" lives)
A single function with priorities:
1. an active client grab (popup grab; button held inside a window — latch until button-up);
2. ImGui popups/modals;
3. the hovered embedded surface — **`ImGui::IsItemHovered()` on the image item**
   (not `io.WantCaptureMouse` — that can't tell "over the client" from "over the title bar");
4. ImGui widgets.

Keyboard focus: `wl_keyboard.enter` (with the array of held keys!) when the surface's
ImGui window is focused and `io.WantTextInput == false`; keep in sync with the
`activated` state. Compositor hotkeys — before everything else.

### 5.3 Coordinates (three corrections, or clicks miss by ~30px in GTK)
item rect → texture scale; + offset of `xdg_surface.set_window_geometry` (CSD shadows
outside the geometry); + `buffer_scale` / viewport. `wl_pointer.frame` after every logical
group (seat v5+). Buttons — evdev (`BTN_LEFT=0x110`). Scroll: `axis` (15/detent) +
`axis_value120`.

### 5.4 Cursors
- `cursor-shape-v1` early (enum → our cursor, no surface rendering);
- legacy `wl_pointer.set_cursor(surface)`: over an embedded window, draw the cursor
  surface on `ImGui::GetForegroundDrawList()` at `mouse_pos - hotspot` (it's an ordinary
  surface: commits, frame callbacks, animation); hide our own cursor.
- Cursor on KMS: composited first (last ImGui draw), then the cursor plane —
  a cursor-only atomic commit on every motion = X11-level latency independent
  of the compositor's fps.

## 6. xdg-shell (we implement it ourselves) — the critical spots

- **Configure dance**: the first configure is `0×0` ("pick your own size"), afterwards the
  size is dictated by the ImGui window's content region; a buffer before the first configure is a
  protocol error (`unconfigured_buffer`); resize — the `resizing` state, no more than once
  per frame, scale the old texture until the client acks; `ack_configure` is the serial
  synchronization point.
- **window geometry ≠ buffer size**: crop UVs by the geometry; sizes in configure —
  geometry; popup anchors are geometry-relative.
- **Decorations**: `zxdg_decoration_manager_v1` → `server_side`. Qt6 will drop CSD,
  GTK4 doesn't implement the protocol — the headerbar stays (accept it).
- **Popups**: the positioner (anchor/gravity/constraint_adjustment: flip→slide→resize),
  configure with coordinates **relative to the parent's geometry** — which is why popups
  follow along for free when the ImGui window is dragged (the client doesn't know where its
  toplevel is). Draw on `GetForegroundDrawList()` (not inside the parent window —
  clipping, z-order); solve constraints against the whole screen; reconfigure on
  reactive (v3+). Grab: a click outside the client's surfaces (including ImGui chrome) ⇒
  `popup_done`, and that click must not fire in ImGui.
- **Subsurfaces are not optional** (mpv, Firefox, toolkit GL/Vulkan areas): a toplevel is
  a tree of surfaces. Composite the tree into an offscreen VkImage on commit → ImGui
  gets a single texture; hit-test over the tree; sync/desync commits per the spec.
- `ping/pong` with a timer; `set_title/app_id` → the title (`"%s###%p"` — a stable
  ID); `set_min/max_size` → `SetNextWindowSizeConstraints`; `close()` — on the close button.

## 7. Protocols by tier (we write all of them ourselves)

- **Tier 0**: `wl_compositor`, `wl_subcompositor`, `wl_shm`, `wl_seat` (v≥5, preferably 8–9),
  `wl_output`, `xdg_wm_base` (Qt6 aborts without it), `wl_data_device_manager`
  (Qt/GTK bind it at startup; clipboard/DnD).
- **Tier 1**: `linux-dmabuf-v1` v4 (effectively tier 0 for GPU clients),
  `zxdg_decoration_manager_v1`, `wp_viewporter` (crop/scale = UV/size of ImGui::Image),
  `wp_presentation` (timestamps from page-flip; mpv A/V sync), `xdg-output`,
  `primary_selection`, `xdg_activation_v1`, `fractional-scale-v1`,
  `single-pixel-buffer-v1` (trivial), `wp_linux_drm_syncobj_v1`.
- **Tier 2**: relative-pointer + pointer-constraints (games), idle-inhibit,
  text-input-v3 (IME), pointer-gestures.
- **XWayland: delegate** — xwayland-satellite (needs only xdg_wm_base +
  viewporter + xdg-output): X11 apps arrive as ordinary wayland toplevels.
  Native integration (being an X11 WM over xcb) is a multi-month subproject, not ours.

## 8. Session and bare metal

- **libseat** (seatd/logind): device fds without root, DRM master drop/regain on
  VT switch. It's not a framework — it's an fd broker. An early alternative: run
  on our own TTY as root with a direct `drmSetMaster`.
- **VT switch**: on disable — stop the frame clocks, `libinput_suspend()`, release
  held keys to clients, `libseat_disable_seat()` (mandatory ack). On enable —
  treat all KMS state as lost: full remodeset with `ALLOW_MODESET`.
  Test with hundreds of switches.
- **Atomic-only**: `DRM_CLIENT_CAP_ATOMIC` (+`UNIVERSAL_PLANES`), property-based,
  `TEST_ONLY` for validation, one in-flight commit per CRTC (`EBUSY`).
- **udev**: monitor the `drm` subsystem (change/HOTPLUG → rescan connectors);
  input hotplug is handled by libinput itself.
- **Multi-monitor**: one DRM fd / VkDevice; per output: its own swapchain of 2–3
  flippable images + its own repaint loop driven by its own flip events (refresh rates differ).
  ImGui: one context per output (shared `ImFontAtlas`), do not use multi-viewport.
- **Overlay planes / direct scanout**: skip — we always do a full composite.
  Revisit only for the fullscreen case.
- **NVIDIA quirks file** upfront: dmabuf with `MOD_INVALID` won't import, LINEAR is
  sampled-only (not renderable), `IN_FENCE_FD` may return `-EPERM` (gamescope
  retries without it), the syncobj protocol is mandatory.

## 9. Nested dev mode

The abstraction proven by gamescope: **the renderer draws into a VkImage whose
presentation it does not own**; two Output backends:
- bare metal: dmabuf-exported images + atomic KMS (flip events = frame clock);
- nested: SDL3 → `SDL_Vulkan_CreateSurface` → `VkSwapchainKHR`
  (acquire semaphore → render → present). Nothing exotic.

The render-pass code targets "VkImageView + extent", not "a swapchain".

## 10. Roadmap

1. **M0 — Vulkan/KMS spike** (separate from the compositor, modeled on kms-vulkan):
   libseat/root → atomic modeset → VkImage allocation with modifiers → dmabuf →
   AddFB2 → flip loop with semaphores in both directions → clean VT switch. Triangle/
   ImGui demo on bare metal. This de-risks the murkiest spot right away.
2. **M1 — compositor skeleton** (nested SDL3+Vulkan or directly on top of M0): libev loop,
   wl_display, `wl_shm` + `xdg_toplevel` (configure dance) → a texture in an ImGui window.
   Criterion: **foot launches and is visible**.
3. **M2 — input**: seat, unified routing (§5.2), keymap, cursors.
   Criterion: foot is usable (typing, selection, scrolling).
4. **M3 — real clients**: `linux-dmabuf-v1` v4 + the implicit-sync bridge,
   subsurfaces (tree composition into a VkImage), decoration, viewporter.
   Criterion: mpv, GTK4/Qt6 with GPU rendering.
5. **M4 — popups/grabs, clipboard/DnD, presentation-time, fractional-scale.**
   Criterion: menus/comboboxes everywhere, copy-paste between clients.
6. **M5 — housekeeping**: syncobj (NVIDIA), the YUV blit pass, cursor plane, hotplug,
   multi-monitor, xwayland-satellite, render-on-demand savings.

## 11. Top risks

1. **Vulkan scanout allocation on non-standard hardware** — the modifier defines the layout,
   but not the placement (contiguity on SoCs); the insurance is the allocator interface with
   the GBM fallback path (~150 lines).
2. **The volume of protocol work** — everything wlroots gave for free (the xdg-shell
   state machine, dmabuf feedback, data-device) is now ours. Compensating: full
   control and Vulkan nativeness.
3. **Focus arbitration ImGui ↔ clients** — one routing function, item-level hit-testing.
4. **The configure/ack/geometry state machine** — mistakes = misclicks and protocol kills.
5. **Event loop discipline** — dispatch → render → callbacks → flush → sleep; never
   sleep without a flush; never forget callbacks for visible surfaces.
6. **imgui_impl_vulkan keeps moving** (the 2026-04 descriptor redesign) — pin the version.
7. **NVIDIA** — quirks, syncobj early.
8. **GEM handle lifetime** and **semaphore reset on SYNC_FD export** — two
   classic sources of silent bugs.

## 12. Dev harness (4 rings)

The development host is macOS; the target is stal/ix (fully static linking, dlopen
replaced by a static dlfcn factory ⇒ any Mesa/Vulkan driver, including lavapipe,
links into the binary directly; OS monorepo: /Users/pg/monorepo/ix). Everything needed is
already in ix: lib/{drm,ev,evdev,input,mesa,seat,udev,vulkan,wayland,xkb},
bin/{sway,labwc,weston,foot,dwl,qemu}; `lib/mesa/soft` = lavapipe.

- **Ring 0 — macOS native (seconds)**: the logic core is platform-independent and
  unit-tested without libwayland (configure state machine, positioner solver,
  focus router, damage). Shell/rendering: ImGui+Vulkan works on the Mac via MoltenVK
  (SDL3) — all chrome (windows/menus/docking/focus) is developed against fake clients
  (stub textures, synthetic input) without Linux. Standalone tests during development
  also run here.
- **Ring 1 — Linux VM on the Mac (QEMU aarch64 + hvf), the main integration loop**:
  a Debian trixie cloud image + cloud-init (vm/create.sh — fully scripted
  provisioning), rsync the sources + build/test inside (./build.sh). ssh in;
  the process crashes, not the machine. Our compositor runs nested under sway in a QEMU
  window (vm/run.sh --gui). Vulkan = lavapipe (mesa-vulkan-drivers).
  The port to stal/ix comes after this ring converges (aarch64/lavapipe in ix is still
  sparse; the real ix machines are x86_64/radv).
- **Ring 2 — KMS in the VM**: the compositor on a VT of the same VM: virtio-gpu KMS (atomic) +
  virtio-input + seatd. The QEMU window shows our output directly; VT switching and
  output hotplug are tested here. Headless CI: vkms(+writeback) or simpler —
  our own readback (`vkCmdCopyImageToBuffer` → PNG → golden-assert).
  Check in M0: lavapipe exports dmabuf via udmabuf (needs /dev/udmabuf
  in the guest; modifiers — LINEAR); whether virtio-gpu will accept AddFB2 on such a buffer —
  if not, the VM fallback: dumb buffer + a copy (the VM ring is not about speed).
  GPU passthrough (venus) doesn't work on a macOS host — and isn't needed.
- **Ring 3 — real hardware (stal/ix, ssh), rarely**: only driver
  specifics (radv/anv/nvk, real modifiers, latency, NVIDIA quirks).
  The "never reboot" rules: launch only from ssh under a supervisor (runit);
  a process crash releases DRM master by itself (fd close) — the VT comes back;
  a stuck VT — `chvt`/SysRq over ssh; the ssh path (ethernet) is independent of graphics.
  A physical reboot — only for a GPU hang.

Cross-cutting plumbing: a **headless implementation of the Output interface** inside the
compositor itself (render to memory + a virtual seat) — for CI and autonomous agent testing;
the client matrix: weston-simple-shm / weston-simple-dmabuf-feedback / foot / mpv /
GTK4/Qt6 demos; our own protocol-test client on libwayland-client (configure dance /
popup grab / clipboard scenarios with asserts); input injection: headless — directly
into the seat, VM — uinput. wlcs (the conformance suite) — optional, later.

## 13. Reference code (keep open while implementing)

- **kms-vulkan** (nyorain) — a minimal end-to-end "Vulkan renders, atomic KMS
  scans out, semaphores in both directions": https://github.com/nyorain/kms-vulkan (vulkan.c)
- **gamescope** — `src/rendervulkan.cpp` (`CVulkanTexture::BInit` — both paths:
  allocate-export and import), `src/Backends/DRMBackend.cpp` (`drm_fbid_from_dmabuf`,
  IN_FORMATS parsing, atomic): https://github.com/ValveSoftware/gamescope
- **wlroots render/vulkan** — as a reference only (not a dependency):
  `vulkan_sync_foreign_texture` / `vulkan_sync_render_buffer` — the canonical
  implicit-sync bridge; texture.c — dmabuf import.
- **kms-quads** (Daniel Stone) — instructional atomic KMS with comments:
  https://gitlab.freedesktop.org/daniels/kms-quads
- **QWaylandQuickItem** — the "window as a widget" model (buffer locking, coordinates,
  subsurfaces): https://doc.qt.io/qt-6/qwaylandquickitem.html
- Protocols: https://wayland.app/protocols/ (xdg-shell, linux-dmabuf-v1,
  linux-drm-syncobj-v1, cursor-shape-v1, presentation-time, viewporter)
- Wayland Book: https://wayland-book.com/ (frame callbacks, positioners, subsurfaces)
- dma-buf sync ioctls (kernel ≥6.0): https://www.collabora.com/news-and-blog/blog/2022/06/09/bridging-the-synchronization-gap-on-linux/
- xwayland-satellite: https://github.com/Supreeeme/xwayland-satellite
- Tooling: drm_info (+ https://drmdb.emersion.fr/ — a database of property support
  by driver)

## 14. As built (2026-07)

Roadmap status: M1–M4 done (clipboard/DnD included; see the production batch
at the end of §14.3). Firefox-esr works (rendering, clicks, menus). Next: M5 —
the stal/ix port.
The code was moved onto the [pg83/std](https://github.com/pg83/std) library (namespace
`stl`): zero dependencies on the C++ STL, ownership via `ObjPool` (creation in a pool,
LIFO death), interfaces in headers / implementations entirely in .cpp, builds only with
clang++ (the library uses clang builtins). Rings 0 and 3 (§12) were never
needed: the whole cycle was rings 1–2 (QEMU aarch64 + lavapipe, headless and KMS).

### 14.1 Layers and files

Project layout (2026-07-14): sources live at the repository root — flat, no
`src/` — one pair of files per subsystem. Everything that is not the compositor
itself lives under `dev/`: `dev/build.sh` (the single build entry point),
`dev/vm/` (the QEMU harness, state in `dev/vm/.state`), `dev/tests/` (ctest
suite), `dev/docs/` (this document). Vendored code stays in `third_party/`.

Dependencies point strictly downward: `main` → `control` → {`wayland`, `renderer`} →
{`scene`, `device`, `output`, `input`, `session`} → `util`. One .h — one .cpp,
with no exceptions: pure interfaces (`output.h`, `input_sink.h`,
`frame_listener.h`, `device_vk.h` aside — it is a data contract) get a .cpp
that is just `#include` of their header, so every header is compile-checked
standalone.

- **Layer 0 — wrappers over kernel mechanisms.**
  - `session.h` — `Session`: a broker of device fds + seat activity events.
    `openDevice/closeDevice` (returns an fd or -errno — no exceptions, called
    from libinput's C callbacks), `seatName()`, `addListener` (`SessionListener`:
    `sessionEnabled/sessionDisabled` = VT switch). Implementations: libseat
    (seatd → logind → builtin: under root without a daemon, libseat spins up its
    embedded seatd itself) and Direct (plain open/close, always active). main tries
    libseat, on failure falls back to Direct with a log line. The DRM node (Device)
    and /dev/input (libinput open_restricted) are opened through Session; headless lives
    without a session. On disable, KmsOutput stops presenting (master revoked),
    libinput suspends; on enable — remodeset with the last shown buffer
    (ALLOW_MODESET) and libinput resume. The ack (`libseat_disable_seat`) is sent after
    the listeners are notified.
  - `device.h` — `Device`: one graphics adapter = a Vulkan device + (optionally)
    a KMS node. Owns the DRM fd, the VkInstance/VkDevice/queue and the
    dmabuf format table; the ev_io on the DRM fd (page-flip events) is its too. Factory
    of its own kind: `createOutput(connector, mode)` and `createRenderer(scene, output,
    frameListener, framesLimit)`. Implementations: `createKms` (a path or nullptr =
    the first node with atomic) and `createHeadless` (lavapipe, no KMS half).
    The Vulkan ↔ DRM correspondence is found via `VK_EXT_physical_device_drm`
    (major:minor against fstat); no match — the first Vulkan device and a
    readback bridge (the VM reality: lavapipe renders, virtio-gpu scans out — an honest
    cross-device case). Handles travel into the Renderer via the internal `DeviceVk`
    contract (device_vk.h, shared only between device.cpp and renderer.cpp).
    `Device::list()` — enumeration for `imway --list`: DRM nodes, connectors with modes
    (preferred marked with `*`), Vulkan devices with their drm nodes.
  - `output.h` — `Output`: `width/height/refresh()` (the display mode; the caller
    fits the scene to it), `start()` (modeset + the first black frame; headless —
    a no-op), `present(pixels)`. KmsOutput = connector+CRTC+plane+mode
    (2 dumb buffers; if the previous flip is still in flight the frame is dropped; the VT is
    put into K_OFF/KD_GRAPHICS), HeadlessOutput = WxH@hz from the config. Selection: connector by
    name ("HDMI-A-1", nullptr = the first connected one), the mode by "WxH@Hz"
    (nullptr = preferred from the EDID).
  - `input.h` — `InputSink`: `motion` (absolute output coordinates), `button/key`
    (raw evdev codes), `scroll` (wheel detents). `InputSource::createLibinput`
    (libinput/udev; outW/outH — the bounds for the relative cursor and the scale of the
    absolute one). `InputSink::tee` fans the stream out to two sinks. Sources don't
    know who consumes. Input devices are an axis separate from Device: the udev seat
    enumerates them, libinput has its own hotplug.
- **Layer 1 — `scene.h`: pure data, not a single wayland/vulkan type in the API.**
  Surface trees (`Surface` + the roles `Subsurface`/`Toplevel`/`Popup`),
  content (BGRA pixels or a `DmabufBuffer`), the applied viewport, the input region,
  view feedback. `SurfaceTexture` is an opaque pointer; only renderer.cpp knows its
  contents. The subsurface stack: `stackBelow` are drawn before the surface,
  `stackAbove` — after, both lists bottom→top. `Toplevel::title/appId` — fixed-size
  buffers: clients change the title constantly, interning strings = growing the pool.
  `Scene::popups` — creation order = stacking order (the last one is topmost).
- **Layer 2 — `wayland.{h,cpp}`: the entire state machine, the sole owner of
  libwayland and xkbcommon.** All globals and protocols, commit semantics, roles.
  Seat is not a subsystem but the protocol input state inside the SM (`SeatState`:
  focus/grabs/keyboard are the protocol's reactions to input). The protocol parts of the
  model (pending, sync caches, xdg resources) are private impl subclasses of the scene
  structures; invisible to the scene and the renderer. Externally: `run()` (loop until quit;
  the epilogue shuts down clients and the display), `sink()` and `frameListener()` — virtual
  accessors, the implementation returns `this`; this way wayland.h doesn't pull in
  input.h/renderer.h.
- **Layer 3 — `renderer.{h,cpp}`: a view of the scene, ImGui+Vulkan, zero knowledge of
  Wayland.** It owns the frame clock itself, pulls node content by `dirty` itself,
  owns the textures itself. Implements `InputSink`: ImGui *is* the window manager,
  it needs raw input (it moves/resizes windows).
- `control.{h,cpp}` — a debug harness over the public APIs of the other layers:
  a FIFO with text commands `motion X Y | button left|right|middle
  press|release | key CODE press|release | type TEXT | scroll N |
  screenshot PATH | quit`; input injection via InputSink, screenshots via
  Renderer, quit via ev_break. Inside — an ascii→evdev table (us layout).
- `main.cpp` — assembles the graph into a single `ObjPool`: creation order = reverse
  death order, the scene dies last. Clients die first (in the epilogue of
  `run()`), their textures go into `orphanedTextures`; subsystems die together
  with the pool. Errors — `stl::Exception` exceptions (`STD_VERIFY`/`Errno().raise`),
  caught at the main boundary.

### 14.2 Contracts between layers

- **`Surface::dirty`** — the SM sets it on commit, the renderer clears it and uploads the
  content into the texture. `dmabuf != nullptr` ⇒ the content is in the dmabuf (pixels are
  empty), otherwise pixels are BGRA, tightly packed rows of w*4.
- **`Scene::orphanedTextures`** — the SM deposits textures of destroyed nodes,
  the renderer frees them on its tick (via `vkQueueWaitIdle` — crude but
  correct). This is the only lifetime decoupling between the SM and the renderer.
- **view feedback** — the renderer writes into the scene from the ImGui frame: `imgX/imgY`
  (the screen position of the Image item), `hovered`, `desiredW/desiredH` (the window's
  content region). The SM reads: pointer picking — by hovered, resize — a configure by
  desiredW/H (deduped against the last sent size).
- **The frame clock belongs to the renderer** (an ev_timer with period 1/hz): a tick without
  `needsFrame` is empty (perfect idle = 0 frames; lavapipe is a CPU), after
  activity 3 settle frames are drawn (hover/ImGui animations). A full frame:
  free orphaned → upload dirty textures → the ImGui frame → `Output::present`
  → `FrameListener::frameShown(msec)`. On it, the SM sends frame callbacks — to all
  trees shown in the frame, including popups (GTK doesn't draw menu content until it
  gets frame done) — and configures per the view feedback.
- **GPU dmabuf formats** — knowledge belonging to Device (`dmabufFormatCount/dmabufFormat`),
  passed into `WaylandConfig` as data: the SM depends on neither the device nor the
  renderer. An empty list = the dmabuf global isn't brought up. Since the formats are
  known before the renderer is born, the graph is built in a single pass: Scene → Device →
  Output → Wayland → Renderer (the FrameListener arrives via the constructor,
  no assemble-later setters).
- Any input and any key press wakes a frame (`needsFrame`), even if ImGui doesn't
  consume the keys.

### 14.3 Deliberate simplifications (divergences from §1–§8)

- **Scanout — the §1.1 swapchain is implemented, with a dumb fallback**: KmsOutput tries
  2 VkImages with DRM modifiers (the intersection Vulkan ∩ the plane's IN_FORMATS,
  confirmed via ImageFormatProperties2 + EXPORTABLE) → export dmabuf →
  `drmPrimeFDToHandle` → `AddFB2WithModifiers`; rendering goes straight into the scanout
  image (finalLayout GENERAL), present = flip its FB, no copies. Any stage failure —
  a log with the stage name and a rollback to the dumb path (readback → memcpy). **In the VM
  only the fallback works**: lavapipe exports via udmabuf successfully, but virtio-gpu
  rejects prime import of a foreign dmabuf (ENODEV) — predicted in §12; the real
  zero-copy test is on stal/ix (radv, render == display). The §2 synchronization isn't
  needed yet: the submit is waited on with a fence before the commit (the frame is ready
  before the flip); the IN/OUT_FENCE_FD fences come together with pipelining. The in-frame
  readback copy remains only on the dumb path; screenshots are done with a lazy one-shot copy.
- **The frame clock**: on KMS there is no timer at all — ev_prepare renders when
  (needsFrame ∨ settle) ∧ ready(), where ready = the modeset happened ∧ no flip in flight
  ∧ the session is active; the page-flip wakes the loop and provides natural vsync pacing,
  ready frames aren't dropped, zero wakeups at idle. Headless stays on the
  ev_timer: it doubles as the test clock (`--frames` on headless counts ticks, on kms —
  actually rendered frames).
- **linux-dmabuf v3, not v4** (no feedback): v1 — format events, v3 —
  modifier events. **The VkImage is cached per wl_buffer** (key — the DmabufBuffer;
  clients cycle 2–4 buffers — one import per buffer, after that only a
  descriptor swap); the death of a wl_buffer travels to the renderer as data
  (`scene.deadDmabufs`), the cached texture lives as long as it is shown. Only
  single-plane buffers are imported;
  ARGB8888/XRGB8888 ↔ `VK_FORMAT_B8G8R8A8_UNORM`, the X variant gets alpha=1
  via a VkImageView swizzle. The buffer is held until replaced by the next one (the renderer
  reads the memory directly, no copies); if the client destroys the wl_buffer while it is
  shown — the memory lives on our fd (the import dups the fd, `vkAllocateMemory` takes the
  duplicate for itself). An import failure sends failed to the client but doesn't bring down
  the compositor.
- **Damage** — a union rectangle (not a list): the SM accumulates
  damage/damage_buffer in pending, on commit clips it and puts it into
  `Surface.damage/damageAll`; all three copies of the shm path (client→pixels,
  pixels→staging, staging→VkImage) are bounded by this rect. Empty damage with a
  new buffer = a full upload (compatibility). damage() with an active
  viewport = full (surface coordinates are ambiguous). The frame render is still
  full (LOAD_OP_CLEAR — ImGui rebuilds everything, §4).
- **Commit semantics**: subsurface sync caches per the spec (the parent's commit
  applies the entire sync subtree; the sync→desync transition applies the accumulated cache;
  the position of any children — including desync — is double-buffered by the parent's
  commit). Two simplifications: dmabuf content is applied immediately even for sync
  children (there's one buffer, nothing to cache), the input region is applied immediately
  (hit-testing must not wait for the parent's commit; good enough for GTK overlays). The
  shm path snapshots the pixels right on commit — the wl_buffer returns to the client
  immediately (important for single-buffered).
- **Input region** — no honest boolean geometry: subtract removes only
  exactly-matching rectangles, partial intersections are ignored
  (sufficient for real clients).
- **The positioner**: anchor/gravity in full, constraint_adjustment
  (flips/slides at the edges) — no: inside an ImGui window the popup is visible anyway.
  Popup cropping by geometry isn't done. `place_above/below` with a non-sibling ref — per
  the spec a protocol error; we forgive it and put it on top (ref == the parent itself — valid).
- **xdg**: the first configure — in response to a commit without a buffer (the spec; GTK
  breaks both ways on this); focus-on-map; the initial ImGui window size — to fit the
  first buffer, after that the user owns the size.
- **Production catch-up (2026-07-14, after "grind through them one by one")**:
  - clipboard: `wl_data_device_manager` v3 in full (selection + DnD with an icon at
    the cursor and the enter/leave/motion/drop cascade; actions — copy>move) +
    `zwp_primary_selection_v1`; offers are re-sent on every keyboard enter; an e2e test
    with wl-copy/wl-paste (both buffers) in ctest.
  - `xdg_toplevel.move/resize` — dragging the ImGui window until button release (resize by
    the edges bits, minimum 120×80); `set_fullscreen` (a window without decorations over the
    whole output, the prev size is restored) and the `activated` state on focus.
  - cursors: `wp_cursor_shape_v1` (enum → a neutral `CursorKind` in the scene →
    ImGui cursors) and the legacy `set_cursor` (the surface in the foreground draw list at
    the hotspot, with frame callbacks — animated cursors live). Applied
    only when the pointer is over a client; over the chrome ImGui owns the cursor.
  - layouts: `--xkb-layout us,ru --xkb-options grp:alt_shift_toggle` —
    the switching is done by xkbcommon itself (the group travels to clients in modifiers);
    a font with Cyrillic: `--font PATH`, the default is DejaVuSans from the system.
  - SIGBUS: libwayland's protection via begin/end_access confirmed by an evil test
    (a client ftruncates the pool out from under us — the compositor survives, the client gets a
    protocol error).
  - the radv block: multi-plane dmabuf import (one fd = a single-memory bind, different
    fds = DISJOINT + BindImageMemory2 per MEMORY_PLANE_i); linux-dmabuf **v4
    feedback** (the format table in a sealed memfd, main_device = the render node from
    `VK_EXT_physical_device_drm`; without a drm node — v3, as on lavapipe);
    **the implicit-sync bridge** — before the submit, wait on the clients' WRITE fences
    (EXPORT_SYNC_FILE → a temporary import into a semaphore), after the frame our
    READ fence into the buffers (export the SYNC_FD semaphore → IMPORT_SYNC_FILE); the gate —
    the device's SYNC_FD semaphores (lavapipe doesn't have them — the bridge is off, logged).
  - small stuff: scrolling on both axes + `axis_discrete` (value120 — when moving to
    seat v8+), `set_buffer_scale` (viewW/H = buffer/scale, damage is
    scaled), `wp_single_pixel_buffer_v1`, `wp_presentation`
    (presented on frame display, CLOCK_MONOTONIC), `xdg_activation_v1`
    (tokens + focus + raising the window), releasing held keys on VT switch
    (the SM subscribes to Session), a 5s ping/pong watchdog (logs hung clients),
    connector hotplug (a udev monitor: disconnect mutes presents,
    reconnect — a remodeset with the last FB).
  - decoration — always `server_side` without negotiation. XWayland — not needed.
  - **Not done**: a second monitor (needs a resizable render pipeline and
    per-output ImGui contexts — a separate project), fractional-scale,
    value120, mode change on hotplug (same mode only).

### 14.4 Pitfalls (found by debugging — do not step on them again)

- **KMS/VT**: `KDSKBMODE K_OFF` + `KDSETMODE KD_GRAPHICS` are mandatory, otherwise
  input is duplicated into the getty beneath the compositor, and the console cursor blinks over the frame.
- **libseat.h has no extern "C" guards** — include it wrapped. Held keys are not yet
  released to clients on VT switch (§8) — debt.
- **Popups are drawn in the ImGui foreground draw list**, not as separate ImGui windows:
  the z-order of separate windows is driven by focus and loses to the toplevel. Their
  hovered state is computed by hand (popups are always on top, nothing can occlude them). The
  position comes from the parent's `imgX/imgY` recorded in the same frame, so popups follow
  the window for free.
- **A press must re-pick its target**: hovered flags update one frame after
  motion; without a re-pick, a click after a single movement misses the client.
- **Picking inside a tree** — the last hovered in draw order (a later Image
  occludes earlier ones; between windows the z-order is already handled by ImGui). Surfaces
  whose input region misses the point are transparent to input.
- **Grab popups**: the keyboard goes to the popup (an override on top of the toplevel's focus),
  a click missing all of the client's surfaces closes grabs in a cascade top-down until
  the one that was hit; after dismiss the keyboard returns to the toplevel. The pointer's
  implicit grab — while at least one button is held, the target is locked. On a toplevel's
  unmap, focus is handed to the last mapped one.
- **frame callbacks**: the wl_resource destructor unlinks the callback from the surface's
  list — take the whole list before iterating, otherwise you iterate a mutating vector.
- **Impl subclasses of scene structures must not shadow its fields**: a `dirty` field
  duplicated in a subclass silently split the SM (writing the shadow) and the renderer
  (reading the base) — content stopped reaching the screen.
- The wheel over a client surface goes to the client, and must not scroll the
  ImGui window itself.
- ImGui: a descriptor pool with headroom (512, fonts + AddTexture), `IniFilename =
  nullptr` (don't litter imgui.ini), `HasMouseCursors` — for the resize cursors at the
  window edges.
- `stl` specifics: `struct Output` conflicts with `stl::Output` under
  `using namespace stl` — write `::Output` in .cpp files; raw string literals don't
  stream into sysO/sysE — use the `_sv` suffix (util.h), char array fields — via a
  `(const char*)` cast.

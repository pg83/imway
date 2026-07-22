# imway

A Wayland compositor built on Dear ImGui: client windows live inside ImGui
windows (textures via `ImGui::Image`), and all the chrome — menus, panels,
decorations — is drawn by ImGui.

## Stack

Raw, no frameworks (no wlroots/Smithay):

- **libwayland-server** — protocols implemented by hand
- **Vulkan** — the only render path, not a single GL/EGL call
  (headless offscreen + readback; ImGui via `imgui_impl_vulkan`)
- **libdrm** — atomic KMS + dumb buffers for scanout
- **libinput + xkbcommon** — input
- **libev** — event loop

## What already works

- wl_compositor, wl_shm, wl_subcompositor (sync/desync, z-order),
  wl_seat v5 (keyboard + mouse, xkb layouts), wl_output,
  xdg-shell (toplevels with interactive move/resize/fullscreen, popups
  with grabs), xdg-decoration (server-side), wp_viewporter,
  zwp_linux_dmabuf_v1 v3/v4 with feedback (multi-plane dmabuf import into
  VkImage, zero-copy), clipboard + drag-and-drop + primary selection,
  cursor-shape + client cursor surfaces, wp_presentation,
  xdg-activation, single-pixel-buffer
- Backends: `headless` (screenshots, input injection via a `--control` FIFO)
  and `kms` (atomic modeset, scanout swapchain with a dumb-buffer fallback,
  page-flip driven frame clock, libinput, libseat session with VT switching,
  connector hotplug)
- foot, mc and Firefox inside ImGui windows, interactive with mouse/keyboard
- live lockscreen on `Super+L`: GPU-blurred desktop, exclusive input routing,
  Unix password verification (`xxx` is the temporary development password)

## Development

```sh
./build                       # .build/imway + all test clients
./build test                  # run the integration suite, report the verdict
./build test -Dfilter='headless_reg_dnd_*' -Druns=5   # a subset, 5 runs each
```

`build` imports `build.py`, resolves project includes transitively and executes
the resulting graph through its content-addressed cache. The integration tests
are graph nodes too: every scenario becomes `runs` command nodes (default 3,
`-Druns=N`) that each start a fresh headless compositor, run the scenario, and
write a JSON verdict — always exiting 0 so a failure never aborts the graph.
One final `test` node depends on all of them, reads the JSONs, prints the
OK / FAIL / FLAKY / SKIP summary and fails the build if anything failed.
`-Dfilter=GLOB` restricts which scenarios build; `-Dallow_flaky` downgrades
flakes to a warning. Because the nodes are cached, tests only re-run when the
compositor, a client, the scenario, or the harness changes. Tests are headless
screenshots with pixel checks: shm, subsurfaces, viewporter, dmabuf (via
udmabuf), popups, protocol errors, and a keyboard e2e test (typing a command
into foot).

The target platform is [stal/ix](https://stal-ix.github.io/): fully static
linking, with the Vulkan driver linked into the binary.

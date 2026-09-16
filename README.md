# imway

[![CI](https://github.com/pg83/imway/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/pg83/imway/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/pg83/imway/branch/main/graph/badge.svg)](https://app.codecov.io/gh/pg83/imway/tree/main)
[![release](https://img.shields.io/github/v/release/pg83/imway?label=release&color=blue)](https://github.com/pg83/imway/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-informational)](STYLE.md)

A Wayland compositor and desktop in one process, drawn with a vendored
ImGui on Vulkan. Windows, a dock, a menu bar, a launcher, notifications,
a calendar, a wifi picker, a volume mixer, a lock screen and a screenshot
tool are all part of the compositor; there is no shell to install next to
it. It drives a display through KMS with atomic modesetting, dma-buf
scanout and HDR, or renders headless for tests and screenshots.

Single-threaded by design, with one background lane for blocking work.
Every object lives in a pool; the C++ standard library is not used, the
vocabulary comes from [libstd](https://github.com/pg83/std). The codebase
follows [STYLE.md](STYLE.md).

## Running

imway is an ordinary process: it starts its own children and reaps them.
Run it from a VT as the user who owns the session, with a seat provider
(seatd or logind) and a session bus:

```
dbus-run-session imway --mode 2880x1800@120 --scale 2 --hdr 300
```

`XDG_RUNTIME_DIR` must be set; the Wayland socket appears there as
`imway-0`. `imway --list` prints the connectors and modes the KMS device
offers.

| Flag | Meaning |
|---|---|
| `--device auto\|headless\|/dev/dri/cardN` | KMS device to drive, or no display at all |
| `--output NAME` | connector to use when the device has several |
| `--mode WxH@HZ` | mode to set; the connector's preferred mode otherwise |
| `--scale K` | UI scale for the compositor's own chrome |
| `--hdr NITS` | enable HDR output with SDR white at this luminance |
| `--hdr-min`, `--hdr-peak`, `--hdr-fall NITS` | override the display's luminance from its EDID |
| `--bpc 8\|10\|12`, `--rgb-range auto\|full\|limited` | link depth and range |
| `--xkb-layout L`, `--xkb-options O` | keyboard map, `us,ru` with `grp:caps_toggle` by default |
| `--font PATH` | UI font |
| `--dpms SEC` | blank the display after this idle time |
| `--login` | start with the lock screen up, before the first frame |
| `--socket NAME` | Wayland socket name |
| `--frames N`, `--screenshot PATH` | render N frames, write the last one and exit |
| `-- CMD ARG...` | a command to start once the compositor is up |

Default shortcuts: Super+F2 opens the launcher, which also runs typed
shell commands; Super+L locks; Alt+Tab switches windows; Super+F12 opens
the inspector; Print takes a screenshot. Shortcuts, theme colors, input
devices, autostart commands and the rest are edited in the settings dialog
from the launcher and live for the session.

The lock screen authenticates through PAM, service `login` by default.
Screenshots are saved as PNG or JPEG XL and cropped in a viewer that is
the same binary: `imway screenshot PATH` opens the crop tool on an image.

Wifi is driven through iwd or NetworkManager, audio volume through sndio or
PulseAudio (which covers PipeWire), whichever is running. Notifications,
status icons and application menus arrive over the session bus.

Clients get xdg-shell with decorations, dialogs, activation, icons and
tags, fractional scale, viewporter, linux-dmabuf with explicit sync, color
management and representation, presentation time, tearing control, fifo
and commit timing, tablet, pointer constraints, gestures and warp, text
input, idle inhibit and notify, image capture, foreign toplevel list, data
control, security context, drm lease and the wlr screencopy and
input-method protocols.

## Building

Linux only. You need clang with C++23 (CI uses clang 21), python3,
pkg-config, glslang, and the development packages for wayland-server and
wayland-protocols, libdrm, libinput, libudev, xkbcommon, libseat, dbus,
libev, vulkan, libpng, libjxl, lcms2, libdisplay-info and lunasvg. sndio,
libpulse and pam are optional and enable the matching providers. The
GitHub Actions setup in `.github/actions/setup` is the reference package
list for Ubuntu 24.04.

```
./build imway            # the compositor, in .build/
./build test             # the test compositor plus every scenario
```

The build tool is `build.py` on top of the shared graph runner; objects
are cached by content. `./build -j N` sets parallelism. With nix, `nix
build` produces the package and `nix develop` a shell with the toolchain.
On an [IX](https://github.com/pg83/ix) machine `dev/build_ix.sh` and
`dev/test_ix.sh` supply the same libraries the `bin/imway` recipe uses and
pass their arguments through to `./build`.

Tests are shell scenarios under `tst/` driving a headless compositor
through a control FIFO, each with a small Wayland client written in C. See
[tst/README.md](tst/README.md) for runs, filters and sanitizer builds.
Format sources with `./dev/style.py`.

## Releases

Releases are numbered tags: `1`, `2`, ... on [GitHub
Releases](https://github.com/pg83/imway/releases).

## License

MIT, see [LICENSE](LICENSE). The vendored `ext/imgui`, `ext/libstd` and
`ext/plt` are MIT as well.

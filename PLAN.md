# Robustness against buggy/malicious clients — survey & improvement plan

A study of how the major Wayland compositors (wlroots, weston, mutter, kwin,
Hyprland, smithay) handle edge cases and defend against buggy or hostile
clients, turned into a concrete improvement plan for imway.

Source trees studied: `tmp/composers/{wlroots,weston,mutter,kwin,Hyprland,smithay}`.
The six verbatim agent reports this synthesis is built from are appended at the
end of this file (§ Appendix), unedited.

---

## TL;DR

imway is already above average in several areas — overflow arithmetic (the only
one of the surveyed compositors with a systematic i64 discipline), serial epochs
(`focusGeneration`, which nobody else has; wlroots' ring-set is the closest),
the 8-cell DnD kill matrix, the poisoned allocator plus 346 tests. The bad news:
five client-reachable holes, three of which take down or hang the whole session:
(1) GPU device-lost = abort / infinite hang, (2) dmabuf fd with no real size
check, (3) one coarse top-level catch — any uncaught throw from a client
callback ends the session. Plus there is no ANR-kill and no ceilings on object
counts.

---

## 1. What the grown-ups do — by topic

### shm and SIGBUS
Everyone who survives solves the same case: a client `ftruncate`s its pool after
mapping, the compositor reads, SIGBUS. Three independent implementations of one
pattern: the handler checks the fault address against a list of active mappings
and **overlays the page with an anonymous `mmap(MAP_FIXED)` of zeros** (wlroots
`wlr_shm.c:182`, kwin `shmclientbuffer.cpp:290` with the "if the fd is sealed
`F_SEAL_SHRINK` the mechanism is unnecessary" optimization, smithay
`pool.rs:323`). weston/mutter delegate to libwayland's
`wl_shm_buffer_begin_access`. Hyprland is the only one **without** this defense.

**imway: closed correctly** — every shm read is wrapped in `begin/end_access`
(`wayland.cpp:1599` and on), there is a `headless_sigbus.sh` test. Plus an addon
libwayland lacks: a `stride < w*4` check (`:1587`) against the fact that
libwayland validates stride only against width.

### dmabuf import
The canonical set (weston/wlroots/smithay nearly identical): plane_idx bounds,
duplicate planes, gaps in the plane set, single modifier, `offset+stride*height`
overflow via u64, **and above all `lseek(fd, 0, SEEK_END)` and cross-check of the
layout against the real fd size** (weston `linux-dmabuf.c:246`, wlroots `:314`,
smithay `mod.rs:1106`; all three tolerant of a non-seekable fd). Separately:
`create_immed` on failure is fatal to the client, deferred `create` is a soft
`failed` (all). wlroots additionally does a trial `drmPrimeFDToHandle` on the
real device.

**imway: parameters are fully validated** (`wayland.cpp:8793-8939`, including the
u32 overflow and the exact plane count per format — stricter than many), a trial
prime import exists (`:8762`). **Hole: no real fd size check.** A small fd with a
formally correct layout goes straight into the Vulkan import — the trust boundary
is pushed into the driver.

### Explicit sync and stuck fences
One model for all: never wait on a fence synchronously. wlroots — the commit of
one specific surface blocks on an eventfd **with no timeout** (spec-correct: a
client that never signals just never presents its frame); smithay — same via a
calloop blocker. The contract (both points required, acquire<release on one
timeline) is checked by everyone at commit as a protocol error.

**imway: the contract is checked** (`:1876-1894`), the release point is signaled
on client death (`~DmabufUse`, `:10047`) — an easy detail to miss. But the wait
model differs: GPU path, on failure a CPU `drmSyncobjTimelineWait` for 100 ms
then "draw unsynchronized" (`renderer.cpp:4111`). Pragmatic and non-blocking for
the loop, but a 100 ms CPU stall per frame from one hostile client is a
framerate DoS. The wlroots way is better: eventfd in libev, the surface commit
parks until the signal.

### xdg-shell: serials, roles, geometry
- `ack_configure`: weston/wlroots — unknown serial = protocol error;
  kwin/Hyprland — lenient "pop everything ≤ acked"; mutter does not validate at
  all. **imway is already in the strict camp** (`:3234`, `INVALID_SERIAL`).
- Roles: everyone funnels through one "set-once, different type = error" choke
  (mutter via GType, smithay via `Option<&'static str>`). **imway: present**
  (SurfaceRole + tests).
- Geometry: interesting fork — kwin/wlroots move toward a strict error (kwin's
  latest HEAD commit is exactly this), mutter deliberately "warn and work
  around." imway is in the strict camp — fine.
- Ping: wlroots/weston 10 s; mutter adds an **event-flood heuristic** (400
  events during a ping = immediately not-alive).

### Unresponsive clients (ANR)
imway's most visible product gap. weston: busy cursor. mutter: dialog +
`SIGKILL` escalation (`delete.c`). kwin: two-phase timer (half the timeout →
unresponsive flag, full → a separate killer process `KWIN_KILLER_BIN`).
Hyprland: `ANRManager` — ping every 1.5 s, N misses → window tint + a
Terminate/Wait dialog that `SIGKILL`s by pid.

**imway: ping every 5 s, on timeout a log line** (`:1207-1216`). That's all.

### DoS and ceilings
- kwin: **`wl_display_set_default_max_buffer_size` = 1 MiB per connection** — a
  client cannot OOM the compositor by flooding requests. Nobody else has an
  explicit one.
- Common ground: damage clamped to the buffer before any math (all), input
  region intersected with the surface rectangle (weston `surface-state.c:499`),
  frame callbacks **sent only to visible surfaces** (weston `compositor.c:4100` —
  a hidden client self-throttles and can't spin the renderer).
- No explicit caps on number of surfaces/callbacks anywhere — all rely on
  libwayland + the OS. This is a deliberate non-defense.

**imway: point caps exist and are good** (mime 64, grants 64, serials 64, icons
1024px/64), damage coalesces into a bbox. Missing: a frame-callback cap
(`:1788`), a buffer-dimension cap before `vkCreateImage`, a per-connection limit
à la kwin.

### Serials and replay
wlroots: a ring-set of handed-out serials with wraparound-safe validation
(`wlr_seat.c:473`). mutter: activation tokens are single-use, bound to real
focus, and on failure demote to `demands_attention` rather than deny (demote
instead of deny — a nice UX pattern). kwin: monotonic selection serials.

**imway: the `focusGeneration` epochs cover replay after focus loss** — stronger
than most; request coverage is broad (`:10576` + move/resize/grab/selection/drag/
activation/cursor).

### abort vs protocol error policy
Unanimous across all six: **client-reachable → typed protocol error and return;
abort/assert only for your own invariants**. weston formalizes it: `xalloc`
(abort) for internal allocations, `zalloc`+`post_no_memory` for client-sized
ones; ICC files are read with a `pread` loop (not mmap!) capped at 32 MB.
wlroots validates enums at set-time so a downstream switch can safely abort.

**imway: the handlers are clean** (the 6 `STD_VERIFY` in wayland.cpp are all
startup), `uploadSurface` already wraps VK_CHECK in try/catch
(`renderer.cpp:1685`). But: `STD_VERIFY` throws, and the only catch is around the
whole `ev_run` (`main_composer.cpp:200`). Any un-wrapped `VK_CHECK` on a
client-influenced path = a clean exit of the whole session. And **imgui
`CheckVkResultFn` = `abort()`**.

### GPU robustness
kwin is the reference: `glGetGraphicsResetStatus` every frame, a bounded 10 s
wait, a full `reinitialize()` + notification. mutter: "taint" — a buffer whose
flip KMS rejected is marked and never retried for scanout (anti black-screen
loop).

**imway: the worst area.** `VK_ERROR_DEVICE_LOST` is handled nowhere,
`vkWaitForFences(UINT64_MAX)` in three places (`renderer.cpp:927, 2842, 4473`) —
a hung GPU hangs the compositor forever; imgui aborts.

### Privileges and sandbox
Hyprland has the most advanced model: security-context + an **allow-whitelist**
of globals for the sandbox (not a denylist!) + a runtime permission manager
(screencast/keyboard/capture — DENY/ASK/ALLOW with a dialog). kwin: filter +
sandbox detection via systemd slice (`app-flatpak-`/`snap.`), forbids nested
sandboxes. mutter: capability model — privileges only for processes the
compositor itself launched; screencast is outside the Wayland socket entirely,
portal-mediated.

**imway: security-context + a `privilegedGlobal` filter already exist**
(`:12479`). Worth checking the filter direction (allowlist vs denylist) and
considering ASK dialogs for capture — cheap with ImGui.

### Tests
weston: `bad-buffer-test` (truncate shm under the compositor), 34 error cases on
color-management alone. kwin: a permanent `dont_crash_*` bucket (one test per
historical crash) + "client dies mid move/resize/lock." mutter: all abuse
clients run **under ASAN**.

**imway: 76 err tests + kill matrices + wire injections + the poisoned allocator
— top-3 territory.** Missing exactly two ideas: an ASAN/UBSAN variant of
`imway_test` and an explicit "don't crash" bucket.

---

## 2. imway action items — prioritized

### P0 — client-reachable abort/hang
1. **GPU device-lost.** (a) All `vkWaitForFences(UINT64_MAX)` → a timeout
   (2–5 s) + treat as device lost; (b) imgui `imguiVkCheck`: `abort()` → throw /
   clean frame-abort path; (c) a minimal recovery policy: clean exit with a code,
   and the supervisor (`main_supervisor.cpp`) grows a "restart the compositor on
   nonzero exit" mode — cheaper than recreating the VkDevice and immediately
   turns hang/abort into a one-second blackout. Full kwin-style device re-init is
   a second step.
2. **dmabuf: fd size check.** In `paramsMakeBuffer`, after the u32 overflow
   check — `lseek(SEEK_END)` and validate `offset`, `offset+stride`,
   `offset+stride*height` (plane 0 only for the height term, don't touch
   subsampled), skipping on a non-seekable fd. ~20 lines, weston
   `linux-dmabuf.c:246` semantics.
3. **Audit un-wrapped `VK_CHECK` on client paths.** `uploadSurface` is already
   correct; sweep the `importDmabuf` neighbors, screenshot/readback and cursor
   paths so client input can't reach the top-level catch. Freeze the weston rule
   in STYLE.md: a client-sized allocation never goes through a throwing macro.

### P1 — DoS and frozen clients
4. **ANR escalation.** You already have the ping and the whole UI stack: 2 missed
   pongs → titlebar tint "not responding" (ImGui), a click on the close cross
   while unresponsive → a Terminate/Wait dialog, kill by pid from
   `wl_client_get_credentials`. Hyprland's `ANRManager` model ports almost
   verbatim.
5. **Ceilings:** a frame-callback cap per surface (e.g. 4096 → protocol error;
   the `client_reg_frame_callback_flood` test exists — tighten it), a `w/h` check
   against `maxImageDimension2D` **before** `vkCreateImage`, and
   `wl_display_set_default_max_buffer_size(1 MiB)` like kwin — one line.
6. **The explicit 100 ms CPU acquire-fence wait** (`renderer.cpp:4111`) → eventfd
   parking of the surface commit (drmSyncobjEventfd is already used in the core —
   the mechanics exist, as in wlroots/smithay). Removes the client-controlled
   frame stall.

### P2 — polish
7. **Scanout taint** (mutter): a dmabuf rejected by the atomic commit in direct
   scanout is marked and not retried — right now a failed flip can loop.
8. **Frame callbacks only to visible surfaces** (weston): check whether
   `fireFrameCallbacks` sends callbacks to occluded/minimized clients — both an
   energy and an anti-spin fix.
9. **Deferred global destruction** (wlroots `wlr_global_destroy_safe`, 5 s) —
   becomes relevant with hotplug outputs / drm-lease.
10. **Tests:** an ASAN/UBSAN build of `imway_test` (the allocator poisoning
    already exists — sanitizers complement, not replace it), a "don't crash"
    bucket for historical crashes, a "client dies mid interactive resize / popup
    grab" test after kwin `testDestroyResizeClient`.

### Explicitly do NOT do
A timeout on the explicit-sync acquire as a protocol error (the spec is against
it, everyone avoids it), limits on the number of surfaces/regions (nobody does
this, libwayland suffices), a strict kill for a bad cursor buffer-scale
(mutter/wlroots deliberately only warn — don't kill a client over cosmetics).

---

# Appendix — verbatim agent reports

The six reports below are the unedited final messages of the research agents,
extracted from the run's jsonl transcripts. Each studied one codebase (or two)
with the same brief: how does it handle edge cases and defend against buggy or
malicious clients, with file:line evidence and "what a from-scratch compositor
could copy" hints.


---

## Appendix A: wlroots

_Verbatim final message of the `wlroots client-robustness study` research agent._

I now have thorough evidence across all areas. Here is my dense report.

---

# wlroots: edge-case handling & defenses against buggy/malicious clients

Repo `f6a01b40` (v0.21.0-dev). All paths absolute-relative to `/home/pg/monorepo/tmp/composers/wlroots/`.

## 1. SHM buffers (`types/wlr_shm.c`, `util/shm.c`)

- **SIGBUS protection for truncated pools**: The compositor writes/reads client-owned mmap'd memory, which SIGBUSes if the client `ftruncate`s the file smaller after mapping. Defense is a global lazily-installed `SIGBUS` handler:
  - `handle_sigbus` (`wlr_shm.c:182-218`): on fault, walks a lock-free atomic singly-linked list `sigbus_data` of active accesses (`wlr_shm_sigbus_data`, line 49-53, `_Atomic next`), checks if `si_addr` is inside any tracked mapping, and if so replaces the page with an anonymous zero mapping via `mmap(MAP_FIXED|MAP_ANONYMOUS)` (line 205) so the access reads zeroes instead of crashing. If addr is not ours, it chains to the previous handler (`reraise`, 212-217).
  - Handler installed only while `wlr_buffer_begin_data_ptr_access` is active (`buffer_begin_data_ptr_access`, 220-258; `SA_SIGINFO|SA_NODEFER`, line 235), uninstalled when the last access ends (`buffer_end_data_ptr_access`, 260-281). Requires lock-free atomic pointers (checked at runtime line 224 and `#error` at compile 18-23).
  - **imway copy hint**: if you ever `memcpy` from a client shm pool on the main thread, you need exactly this SIGBUS trampoline, or a malicious client shrinking its pool crashes your compositor. Cheapest alternative: never touch shm CPU-side (always upload via GPU which faults in the driver/other process).
- **Resize rules** (`pool_handle_resize`, 375-395): shrinking is forbidden (`size < mapping->size` → `WL_SHM_ERROR_INVALID_STRIDE`); growth re-mmaps a fresh `wlr_shm_mapping` and *keeps the old one alive* (`mapping_drop`/refcount-ish `dropped` flag, 119-146) while an access is in flight — old mapping freed only when both `dropped` and not referenced by any live `sigbus_data`.
  - **imway hint**: the "keep old mapping alive during access" pattern (COW between resize and in-flight read) is worth copying.
- **create_buffer overflow/stride/format validation** (`pool_handle_create_buffer`, 314-373): `offset<0 || width<=0 || height<=0 || stride<width || offset + (uint64_t)stride*height > pool->mapping->size` — note the deliberate `uint64_t` cast to avoid overflow (comment line 319). Format checked against advertised set (`shm_has_format`), then `drm_get_pixel_format_info` must exist, then `pixel_format_info_check_stride`.
- **create_pool** (`shm_handle_create_pool`, 419-464): `size<=0` rejected; fd always `close()`d on every error path.
- **fd hygiene** (`util/shm.c`): `shm_open` guarantees CLOEXEC (comments 29, 66); `allocate_shm_file_pair` (59-97) creates an RW+RO pair, `shm_unlink`s immediately, and `fchmod(rw_fd,0)` so the RO fd can't be reopened RW via `/proc/self/fd` (comment 76-77) — used for the dmabuf feedback table fd handed to clients.
- **Mandatory formats**: `wlr_shm_create` asserts ARGB8888+XRGB8888 present (517) per spec.

## 2. DMABUF import (`types/wlr_linux_dmabuf_v1.c`)

- **plane index / double-set / modifier consistency** (`params_add`, 149-198): `plane_idx >= WLR_DMABUF_MAX_PLANES` → `ERROR_PLANE_IDX`; re-adding a plane → `ERROR_PLANE_SET`; a differing modifier across planes → `ERROR_INVALID_FORMAT`. Every error path `close(fd)`. Params become inert after use (`wl_resource_set_user_data(...,NULL)`, 246) → re-use → `ERROR_ALREADY_USED`.
- **create-time validation** (`params_create_common`, 230-399): no planes → `ERROR_INCOMPLETE`; plane 0 missing → `INCOMPLETE`; gap in planes (fd[3]/fd[2] set but fd[2]/fd[1] == -1) → `INCOMPLETE` (263-269); unknown flags rejected via generated `..._flags_is_valid` (273); non-zero flags (Y-invert/interlaced) → soft-fail (unsupported); `width<1||height<1` → `INVALID_DIMENSIONS`.
- **Size vs actual fd size** (296-343): per-plane `offset+stride > UINT32_MAX` and `offset + stride*height > UINT32_MAX` → `OUT_OF_BOUNDS`; then `lseek(fd,0,SEEK_END)` — **if lseek fails it *skips* the check** (comment "Skip checks if kernel does not support seek", 314-316); else validates `offset > size`, `offset+stride > size || stride==0`, and for plane 0 `offset+stride*height > size`. Note subsampled planes >0 are deliberately NOT height-checked (comment 335).
- **Real import test** (`check_import_dmabuf`, 208-228): default callback actually `drmPrimeFDToHandle` + `drmCloseBufferHandle` on the compositor's render node to prove the kernel accepts the fd; skipped if no render node (`main_device_fd<0`). Note the `// TODO: check number of planes` (215) — plane count vs format is *not* validated here.
- **create_immed failure policy** (`err_failed`, 383-396): for deferred `create`, sends `failed` event; for `create_immed` it deliberately posts a fatal `ERROR_INVALID_WL_BUFFER` and kills the client rather than hand back a zombie buffer (comment 387-391).
- **Feedback table** (`feedback_compile`, 509-626): builds a format/modifier table in an RO shm fd (via `allocate_shm_file_pair`), per-tranche index arrays (`uint16_t`), asserts every tranche format exists in the fallback tranche. Per-surface feedback lives as a surface addon; feedback resources become inert on manager destroy (`surface_destroy`, 690-703, links reset).
- **imway hint**: copy the `lseek(SEEK_END)` bounds check and the uint32 overflow guards verbatim; and the "actually try to import on the real device before accepting" step catches a huge class of malicious fds.

## 3. Explicit sync / `linux-drm-syncobj` (`types/wlr_linux_drm_syncobj_v1.c`, `render/drm_syncobj.c`)

- **Never-signalling acquire fence**: the commit is *blocked indefinitely* — there is **NO timeout**. `lock_surface_commit` (237-269) checks if the point already materialized (`DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE`); if not, it takes a `wlr_surface_lock_pending` cached-state lock and registers a `wlr_drm_syncobj_timeline_waiter` that fires on a DRM eventfd. Until the fence signals, the surface's cached commit stays locked. The only release paths are the fence signalling (`surface_commit_handle_waiter_ready`) or the surface being destroyed (`surface_commit_handle_surface_destroy`, 229-234). **This is a deliberate non-defense**: a client that never signals just never presents that surface — it doesn't block others (per-surface cached-state queue). This mirrors what libwayland does with buffers.
- **Waiter machinery** (`drm_syncobj.c:188-249`): `drmSyncobjEventfd` arms an eventfd added to the wl event loop; `handle_eventfd_ready` reads it and calls the callback. eventfd is `EFD_CLOEXEC`. Requires `DRM_CAP_SYNCOBJ_TIMELINE` and a working eventfd — probed at creation with an intentional invalid-handle wait expecting `ENOENT` (`check_syncobj_eventfd`, 426-442); disabled otherwise.
- **Protocol validation** (`surface_handle_client_commit`, 271-321): buffer w/o acquire point → `NO_ACQUIRE_POINT`; buffer w/o release point → `NO_RELEASE_POINT`; acquire/release point set w/o buffer → `NO_BUFFER`; acquire≥release on the *same* timeline → `CONFLICTING_POINTS`. Inert surface (destroyed) → `NO_SURFACE` on set_acquire/release_point.
- Timeline import failures → `ERROR_INVALID_TIMELINE` (371-384); drm_fd is `F_DUPFD_CLOEXEC`-duped (463).
- **imway hint**: the "block just this surface's cached state on an eventfd, release on fence-or-surface-destroy, no timeout" design is the correct minimal model; don't add a timeout (spec doesn't want one).

## 4. Protocol object lifetime / inert resources

Pervasive, well-documented pattern. Key mechanisms:
- **Role-object-before-surface ordering**: destroying a `wl_surface` while its role object (xdg_surface/subsurface/etc.) still exists → `WL_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT` (`wlr_compositor.c:56-66`); same for xdg_surface (`wlr_xdg_surface.c:231-246`) and `xdg_wm_base` destroyed with live surfaces → `XDG_WM_BASE_ERROR_DEFUNCT_SURFACES` (`wlr_xdg_shell.c:44-56`).
- **Inert resource pattern**: on parent/global/surface destroy, resources are made inert via `wl_resource_set_user_data(res, NULL)` and every request handler null-checks (`surface_from_resource` returns NULL → early return or posts `NO_SURFACE`). Examples: subsurface becomes inert on parent-surface destroy (`wlr_subcompositor.c:9,27-45,253-260`), syncobj surface (`wlr_linux_drm_syncobj_v1.c:57-63,79-88`), data offer (`wlr_data_offer.c:191-215`), activation token (`wlr_xdg_activation_v1.c:22-44`), dmabuf params (line 246).
- **Leak/reentrancy asserts**: after emitting a `destroy` signal, code asserts the signal's `listener_list` is empty (`assert(wl_list_empty(&...->events.destroy.listener_list))`) — e.g. `wlr_compositor.c:732-737`, `wlr_xdg_surface.c:535-539`, `wlr_drag.c:166-169`, `wlr_linux_dmabuf_v1.c:862`. This catches listeners that failed to unregister (which would be a use-after-free). It's a debug-only crash but effectively enforces the destroy contract on downstream code.
- **Delayed global destruction** (`util/global.c`, `wlr_global_destroy_safe`): a `wl_global` is `wl_global_remove`'d, user_data nulled as a safety net, then actually destroyed 5 s later on a timer — works around the wayland race where a client binds a global it hasn't yet learned is gone (references freedesktop wayland issue #10). **imway hint**: copy this verbatim for any hot-pluggable global (outputs, seats).
- **Two-phase surface destroy**: `surface_handle_resource_destroy` (`wlr_compositor.c:724-766`) destroys role object first, emits destroy, finishes addons, then asserts all six event signal lists empty.

## 5. xdg-shell (`types/xdg_shell/`)

- **ack_configure serial validation** (`wlr_xdg_surface.c:63-124`): the serial must match a configure in `configure_list`; else `XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE` "wrong configure serial". Acking is cumulative — all older configures are dropped and their `ack_configure` events emitted. Ack before role → `NOT_CONSTRUCTED`.
- **Configure is idle-batched** (`wlr_xdg_surface_schedule_configure`, 164-179): one serial per idle cycle via `wl_display_next_serial`, coalescing multiple schedule calls.
- **Unconfigured buffer**: committing a buffer before first configure → `XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER` (`xdg_surface_role_client_commit`, 286-290); buffer attached at xdg_surface creation → same error (405-410).
- **Invalid window geometry**: `set_window_geometry` with `width<=0||height<=0` → `INVALID_SIZE` (218-222). An *effective* geometry (intersection of extents and set geometry) that is empty currently only logs a warning ("will result in client disconnection in the future", 266-275) and falls back — **a deliberate not-yet-enforced case**.
- **Role conflicts**: `set_xdg_surface_role` rejects re-assigning a different role or reassigning while a role object exists → `XDG_WM_BASE_ERROR_ROLE` (461-485); core `wlr_surface_set_role`/`set_role_object` similar (`wlr_compositor.c:890-927`).
- **Popup edge cases** (`wlr_xdg_popup.c`):
  - `grab` after popup already mapped → `INVALID_GRAB` (287-292); invalid seat → destroys popup (silently dismiss); grab not on topmost popup → `NOT_THE_TOPMOST_POPUP` (297-302); destroy while not topmost → `NOT_THE_TOPMOST_POPUP` (350-355).
  - Grab dismiss vs error: pointer button on a serial that produced no event, or click/touch-down outside the grabbing client → `xdg_popup_grab_end` sends `popup_done` to all popups and ends the seat grabs (71-81, 141-152) — graceful dismiss, not a protocol error.
  - Popup w/o parent at commit → `NOT_CONSTRUCTED` (247-253); popup parent w/o role → `INVALID_POPUP_PARENT` (382-386).
- **Positioner validation** (`wlr_xdg_positioner.c`): `set_size` w/ `<1` → `INVALID_INPUT` (20-25); `set_anchor_rect` negative → `INVALID_INPUT` (37-42); anchor/gravity/constraint_adjustment validated against generated `_is_valid` enum guards (56, 72, 89). Incomplete positioner (size or anchor_rect width 0) at popup create/reposition → `INVALID_POSITIONER` (`wlr_xdg_popup.c:327-331,372-376`). Constraint solving (flip/slide/resize, 340-518) is fully client-input-driven but arithmetic-safe (works on ints, resize rejects empty box).
- **Ping/pong unresponsive clients** (`wlr_xdg_surface.c:554-566`, `wlr_xdg_shell.c:32-42,81-91`): `wlr_xdg_surface_ping` sets `ping_serial` and arms a 10 s timer (`ping_timeout`, default set at `wlr_xdg_shell.c:153`); `pong` with mismatched serial is ignored; timeout emits `ping_timeout` event per surface and clears serial. wlroots does **not** itself kill unresponsive clients — it surfaces the event and lets the compositor decide.

## 6. Resource/DoS limits, region, reentrancy, damage/scale/transform

- **Damage clamping** (`wlr_compositor.c`): negative `damage`/`damage_buffer` w/h silently ignored (95-97, 631-633); on commit, surface damage intersected to `[0,0,width,height]` and buffer damage to buffer extents (`surface_finalize_pending`, 240-245) — a client can't make damage regions grow unbounded server memory beyond the surface.
- **Region complexity**: `wl_region` is a raw pixman region (`wlr_region.c`); `add`/`subtract` just union/subtract rects. **No explicit rectangle-count cap** — wlroots relies on pixman + libwayland message-size limits. Deliberate non-defense.
- **Frame-callback flooding**: callbacks are appended to `pending.frame_callback_list` (`surface_handle_frame`, 108-125) and only destroyed on `frame_done` or surface teardown. **No per-surface cap** — relies on libwayland's per-client resource/fd limits. Deliberate non-defense.
- **buffer size limits**: cursor path applies hardware limits (`types/output/cursor.c:199-221`); general surface buffer size bounded only by the shm/dmabuf size validation above.
- **scale/transform validation**: `set_buffer_scale <= 0` → `INVALID_SCALE` (614-624); `set_buffer_transform` outside `[NORMAL, FLIPPED_270]` → `INVALID_TRANSFORM` (601-612); buffer size not divisible by scale → `INVALID_SIZE` *except* for cursor surfaces where it's only logged (`surface_finalize_pending`, 209-227, references wayland issue #194). Output-management transform/scale validated: `wl_output_transform_is_valid`, `scale<=0` → errors (`wlr_output_management_v1.c:258-284`).
- **Listener-list reentrancy**: wlroots uses `wl_signal_emit_mutable` (safe against listeners removing themselves during emit) throughout; combined with the post-emit `assert(wl_list_empty(listener_list))` on destroy signals this both allows reentrant removal and detects leaks. **imway hint**: your signal-emit must tolerate a listener destroying itself or an adjacent listener mid-emit (use a safe-iteration + per-listener "emitting" cursor like `wl_signal_emit_mutable`).
- **Cached-state locking** (`wlr_compositor.c:942-987`): `wlr_surface_lock_pending`/`unlock_cached` implement an ordered queue of cached commits (used by subsurface sync + syncobj + viewport). Commits apply in order; a lock can't jump the queue (971-975). Bounded only by client commit rate.

## 7. Serial validation & replay protection (`types/seat/`)

- **Serial ring-set** (`wlr_seat.c:473-524`): `wlr_seat_client_next_serial` records every serial it hands a client in a compact `wlr_serial_ringset` of contiguous ranges (`WLR_SERIAL_RINGSET_SIZE` buckets). `wlr_seat_client_validate_event_serial` (500-524) rejects serials that are "newer than current" (`rev_dist >= UINT32_MAX/2`, wraparound-safe) or that fall in a gap the client was never given; when the set is full it conservatively accepts very old serials (can't prove they weren't recycled). This is genuine **replay/forgery protection** for serials.
- **Used by**: `set_selection` (`wlr_data_device.c:147`) and primary selection; also `set_selection` additionally rejects a serial older than the current selection serial (superseded, 153-158).
- **Grab-serial validation** (`wlr_seat_pointer.c:603-621`, `wlr_seat_touch.c:472-497`): `wlr_seat_validate_pointer_grab_serial` requires `button_count==1 && grab_serial==serial` and (optionally) that `focused_surface==origin`; touch analog requires exactly one point and matching origin surface. `grab_serial` is captured when a button/down actually produced a client event (`wlr_seat_pointer.c:513-517`, `wlr_seat_touch.c:201-209`).
- **Delegation caveat**: for `xdg_toplevel` move/resize/show_window_menu and `wl_data_device.start_drag`, wlroots does **not** validate the serial itself — it just packages the serial into an event (`wlr_xdg_toplevel.c:288-343`, `wlr_drag.c:492-508`) and expects the compositor to call the `validate_*_grab_serial` helpers. `start_drag` only rejects if a drag is already in progress. **imway hint**: implement the ring-set; but note the *policy* of which requests must validate is partly the compositor's job — don't assume the shell layer did it.

## 8. DnD / selection edge cases (`types/data_device/`)

- **Source destroyed mid-drag**: offers listen on `source->events.destroy`; `data_offer_handle_source_destroy` (`wlr_data_offer.c:230-237`) nulls `offer->source` *before* destroying the offer so it won't double-free the source. On focus change during an active (not-yet-dropped) drag, offers are destroyed without destroying the source (`wlr_drag.c:39-52`).
- **Offer/receive after source gone**: `data_offer_handle_receive` on an inert offer (`offer==NULL`) still **closes the passed fd** (`wlr_data_offer.c:94-100`) — no fd leak. `accept` on non-drag offer ignored (85-89).
- **fd hygiene**: `client_data_source_send` closes the fd after forwarding (`wlr_data_source.c:106-112`); inert receive closes fd (above); shm/table fds CLOEXEC (§1). Pipe/xwm paths elsewhere use `set_cloexec` (`util/fd.c`).
- **mime handling**: `data_source_offer` (`wlr_data_source.c:184-219`) dedups mime types, `strdup`s with NoMemory handling; mime strings from clients are passed through, not parsed. `set_actions` after finalize (post start_drag/set_selection) → `INVALID_ACTION_MASK` (174-179); invalid action mask bits → error (167-172).
- **Offer action negotiation** (`wlr_data_offer.c:22-76,155-189`): validates action masks (`~DATA_DEVICE_ALL_ACTIONS` → `INVALID_ACTION_MASK`), preferred action must be a single bit within the mask (`__builtin_popcount>1` → error). `finish` before accept → `INVALID_FINISH`; finish w/ NONE/ASK action → error (123-153).
- **drag drop / cancel** (`wlr_drag.c:132-180,213-248`): `drag_destroy` guards reentrancy with `cancelling` flag; ends grabs *before* emitting destroy so focus can change; drops only if `current_dnd_action && accepted`, else destroys source. Icon surface gets `drag_icon_surface_role` (`wlr_data_device.c:66-71`), destroyed with the drag.
- **imway hint**: the "inert offer still closes the receive fd" and "null source pointer before offer destroy to avoid double-free" details are easy to get wrong — copy them.

## 9. Subsurface (`types/wlr_subcompositor.c`)

- **Cycle/self-parent prevention** (`subcompositor_handle_get_subsurface`, 308-386): `wlr_surface_get_root_surface(parent) == surface` → `WL_SUBCOMPOSITOR_ERROR_BAD_PARENT` "cannot be a parent of itself or its ancestor" (327-334). `wlr_surface_get_root_surface` (`wlr_compositor.c:989-995`) walks up the subsurface chain, so this catches deep cycles, not just direct self-parenting.
- **place_above/below sibling validation**: target must be the parent or an actual sibling (`subsurface_find_sibling`, 76-93) else `WL_SUBSURFACE_ERROR_BAD_SURFACE` (95-153).
- **Orphaning on parent destroy**: `subsurface_handle_parent_destroy` (253-260) destroys the subsurface object entirely (comment: client has no way to use it anymore) — the parent-state is a `wlr_surface_synced` so it's cleanly removed. Also becomes inert (user_data NULL) via `subsurface_destroy`.
- **Sync tree** (`subsurface_is_synchronized`, 13-23): walks up parents; a subsurface is synced if it or any ancestor is synced. Cached-commit locking (`surface_client_commit`/`parent_commit`, 262-294) holds at most one cached commit per subsurface (`has_cache` guard prevents unbounded queueing, 269-273), released on desync or parent commit. Role-object destroy + map/unmap cascade through children (`wlr_compositor.c` map/unmap recursion, 838-871).
- **imway hint**: the root-surface-walk check for cycles and the "one cached commit max while synchronized" bound are both worth copying.

## 10. Other notable items

- **Overflow-checked arithmetic**: `render/pixel_format.c` — `pixel_format_info_min_stride` guards `width > INT32_MAX/bytes_per_block` (271-279); `pixel_format_info_check_stride` enforces stride divisibility and min-stride (281-300); `pixels_per_block` clamps 0→1. shm/dmabuf use explicit `uint64_t` casts before multiply (§1, §2). `util/array.h` provides `array_remove_at`/`array_realloc` helpers.
- **Defensive-assert vs client-triggerable abort policy**: The idiom is `wl_resource_post_error` for anything a client can trigger, and `assert`/`abort` only for compositor-internal invariants. The `abort()`/`assert(0 && "not reached")` sites (grep results) are all on enum-exhaustiveness / unreachable branches (`wlr_xdg_positioner.c:207`, `wlr_xdg_surface.c:103` etc.), **not** reachable by client input — e.g. anchor/gravity are validated at set-time so the switch's `abort()` is truly unreachable. `wlr_surface_reject_pending` (`wlr_compositor.c:874-888`) is the standard "reject a commit atomically" primitive (asserts it's called during commit handling, sets `pending_rejected` so the commit is dropped). **imway hint**: adopt the rule "client-reachable → post_error; internal invariant → assert" and validate enums at set-time so downstream switches can safely abort.
- **UTF-8 validation**: toplevel `set_title` rejects invalid UTF-8 (`wlr_xdg_toplevel.c:228-232`, `util/utf8.c`); note `set_app_id` does **not** validate UTF-8 (245-259) — a deliberate asymmetry.
- **Security context** (`types/wlr_security_context_v1.c`): supports sandboxing via a listen fd; state copied defensively (`security_context_state_copy`). (Global-filtering policy itself lives in the compositor via libwayland's global filter, not in this file.)
- **min/max size sanity** (`wlr_xdg_toplevel.c:143-146`): rejects negative min/max and `max < min` at commit.

### What wlroots deliberately does NOT defend against
- **No timeout on explicit-sync acquire fences** (§3) — a client that never signals just never presents; correct per spec, but means a compositor that *synchronously* waits on such a fence would hang (wlroots doesn't — it's event-driven).
- **No cap on region rectangle count, frame-callback count, or number of surfaces/subsurfaces** (§6) — relies on libwayland's per-client fd/message limits and the OS. A client can still consume server memory with pathological regions/commits.
- **No fuzzing harness in-tree** (only `test/test_box.c`, `bench_scene.c`, vulkan staging test; no libFuzzer/AFL targets).
- **Empty effective window geometry** only warns, doesn't yet disconnect (§5).
- **Split display/render GPU setups**: dmabuf import checks are *skipped* when the device has no render node (`wlr_linux_dmabuf_v1.c:909-916`) — malicious fds pass unchecked there.
- **Serial validation for move/resize/start_drag is delegated** to the compositor (§7) — wlroots ships the mechanism but not the enforcement at the shell layer.
- **dmabuf plane-count-vs-format not validated** (`// TODO`, line 215); **lseek-unsupported fds skip size checks** (line 314-316).


---

## Appendix B: weston

_Verbatim final message of the `weston client-robustness study` research agent._

I now have comprehensive evidence across all ten areas. Here are my dense factual findings.

---

# Weston: edge-case handling & defense against buggy/malicious clients

Repo root: `/home/pg/monorepo/tmp/composers/weston` (git HEAD `c634a4a`). Reference Wayland compositor + libweston.

## 1. wl_shm SIGBUS handling (`wl_shm_buffer_begin_access`)

The SIGBUS handler itself lives in **libwayland-server** (not in this tree); weston's job is to wrap every raw read of shm pool memory in `wl_shm_buffer_begin_access()` / `wl_shm_buffer_end_access()`, which lets libwayland longjmp out of a client-truncated mapping instead of crashing. Every renderer/backend that touches shm data guards it:
- GL: `libweston/renderer-gl/gl-renderer.c:1190,1264,3412,3432` (texture upload + readback).
- Pixman: `libweston/pixman-renderer.c:312,547`.
- Vulkan: `libweston/renderer-vulkan/vulkan-renderer.c:1403,2869,2887`.
- No-op: `libweston/noop-renderer.c:109`.
- DRM dumb-buffer shadow blit: `libweston/backend-drm/drm.c:892,3617`.
- VNC framebuffer: `libweston/backend-vnc/vnc.c:608`.

Prevention side (`shared/os-compatibility.c:182-259`): `os_create_anonymous_file()` prefers `memfd_create()` with `MFD_ALLOW_SEALING` then applies `F_SEAL_SHRINK` (os-compatibility.c:214) so *weston's own* shm buffers can never be shrunk under it — SIGBUS becomes impossible for them. Falls back to `posix_fallocate` (guarantees ENOSPC not SIGBUS) then `ftruncate`. Also `MFD_NOEXEC_SEAL` with graceful EINVAL fallback for kernels <6.3 (os-compatibility.c:199-205).

Note: there is a real guard in `gl-renderer.c:3400` — a shm `wl_buffer` can be destroyed by the client before damage is flushed ("wayland-server just nukes the wl_shm_buffer from underneath us"), so it null-checks `buffer->shm_buffer` and bails.

*Copy hint:* wrap every read of client shm memory in begin/end_access and use sealed memfds for your own shm; treat `shm_buffer` as something that can vanish before you use it.

## 2. Buffer validation

**shm** (`libweston/compositor.c:3102-3116`, `weston_buffer_from_resource`): pulls width/height/stride from wl_shm; crucially rejects unknown/hidden formats — `pixel_format_get_info_shm()` returns NULL → `goto fail` (compositor.c:3115). `hide_from_clients` formats are also rejected. Note stride/size sanity is largely delegated to libwayland's wl_shm (which validates `offset+stride*height <= pool_size`).

**dmabuf** (`libweston/linux-dmabuf.c`) — this is the thorough one:
- `params.add`: plane_idx `>= MAX_DMABUF_PLANES` → `PLANE_IDX` error (linux-dmabuf.c:102); double-set of a plane → `PLANE_SET` (110); modifier mismatch between planes → `INVALID_FORMAT` (124); reusing a consumed params object → `ALREADY_USED` (92).
- `params.create` (`params_create_common`, linux-dmabuf.c:196-275): no planes → `INCOMPLETE`; holes in plane set (e.g. [0,1,3]) → `INCOMPLETE` (204); width/height <1 → `INVALID_DIMENSIONS` (218); `offset+stride > UINT32_MAX` and `offset + stride*height > UINT32_MAX` overflow checks with explicit `(uint64_t)` casts → `OUT_OF_BOUNDS` (228-242); then **`lseek(fd,0,SEEK_END)`** to get real fd size (246) and checks `offset >= size`, `offset+stride > size`, `offset+stride*height > size` → `OUT_OF_BOUNDS` (250-274). Deliberately tolerant: if `lseek` returns -1 it `continue`s because "the kernel [may] not support seeking on dmabuf" (244-248).
- create_immed failure is treated as fatal (`INVALID_WL_BUFFER`, kill client) rather than handing back an invalid buffer — explicitly documented choice (linux-dmabuf.c:320-329).

**viewport** (`wp_viewporter`, compositor.c):
- `set_source`: the four-args-all-`-1` unset case; otherwise `w<=0||h<=0||x<0||y<0` → `WP_VIEWPORT_ERROR_BAD_VALUE` (compositor.c:9527).
- `set_destination`: `-1,-1` unset; else `<=0` → `BAD_VALUE` (9572).
- `get_viewport` twice on one surface → `WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS` (9609).
- At commit (compositor.c:5347-5365): src rect outside buffer → `WP_VIEWPORT_ERROR_OUT_OF_BUFFER`; non-integer dst when no explicit dst size → `WP_VIEWPORT_ERROR_BAD_SIZE`. The source-validity check (`weston_surface_is_pending_viewport_source_valid`, 5220-5275) does explicit `wl_fixed` overflow guards (`> wl_fixed_to_int(INT32_MAX)`, 5261) and `fixed_sum_gt` bounded addition.

**transform/scale** (compositor.c): transform `<0 || > WL_OUTPUT_TRANSFORM_FLIPPED_270` → `INVALID_TRANSFORM` (5413); scale `<1` → `INVALID_SCALE` (5433); buffer size not integer-multiple of scale/transform → `WL_SURFACE_ERROR_INVALID_SIZE` at commit (5312, also the no-buffer/old-buffer path 5336). attach with nonzero offset on wl_surface v5+ → `WL_SURFACE_ERROR_INVALID_OFFSET` (5040).

*Copy hint:* validate dmabuf fds with `lseek(SEEK_END)` and `(uint64_t)` overflow math but tolerate non-seekable fds; reject unknown pixel formats at import; do all viewport/scale bound checks at commit, not just at request time.

## 3. xdg-shell / libweston-desktop lifecycle

**Configure serial tracking** (`libweston/desktop/xdg-shell.c`): each `xdg_surface` keeps a `configure_list` of pending `{serial,...}` structs (xdg-shell.c:80-91). `ack_configure` (1806-1868) walks the list: serials `< acked` are discarded, `== acked` matched, `>` stop; if none matched → `XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE` "Wrong configure serial" (1847). Acking on an already-destroyed role object → `XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT` (1813). Configures are coalesced via an idle source (`configure_idle`, `weston_desktop_xdg_surface_schedule_configure`).

**Role lifecycle**: `weston_surface_set_role()` (compositor.c:5650-5673) is the single choke point — reassigning a different role posts the caller-supplied error/code; same role name is idempotent. xdg wraps it (`weston_surface_set_role(... xdg_toplevel_role ...)` xdg-shell.c:1644). Buffer attached before first configure → `XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER` (xdg-shell.c:1897, 2078). Committing with no role → `XDG_SURFACE_ERROR_NOT_CONSTRUCTED` (1917). move/resize/show_window_menu before configure → `NOT_CONSTRUCTED` (787,816,845).

**Maximize/fullscreen state races** (xdg-shell.c:1119-1145): at commit, if `next.state.maximized` but geometry ≠ configured size → `INVALID_SURFACE_STATE`; if fullscreen and geometry *larger* than configured → `INVALID_SURFACE_STATE`. State is only latched into `current` after these pass (1147).

**Popup grabs & dismissal**: `xdg_popup.grab` validates a valid seat mapped to a desktop-seat (else no_memory, xdg-shell.c:1291); grab after map → `XDG_POPUP_ERROR_INVALID_GRAB` (1298); not created on topmost popup → `XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP` (1312). Dismissal in `libweston/desktop/seat.c`: a button up outside the grabbing client, or after the initial-up and >500ms since grab, ends the grab (`weston_desktop_seat_popup_grab_pointer_button`, seat.c:126-144); keyboard grab cancel also ends it (seat.c:78-83). Popup pointer focus is filtered to the grab client only (seat.c:106). `reposition` requires a complete positioner (`is_positioner_valid`, size & anchor_rect non-zero) else `XDG_WM_BASE_ERROR_INVALID_POSITIONER` (1352). Positioner input validation → `XDG_POSITIONER_ERROR_INVALID_INPUT` (582-642).

**Ping/pong (client-unresponsive UX)**: `libweston/desktop/client.c` — one ping timer per client, **10 s** timeout (`wl_event_source_timer_update(..., 10000)`, client.c:207); reentrancy guarded by `ping_serial != 0` (client.c:202); pong ignored if serial mismatches (client.c:217). On timeout the desktop-shell marks every surface of the client `unresponsive` and shows a **busy/spinner cursor** (`desktop_surface_ping_timeout` → `set_busy_cursor`, `desktop-shell/shell.c:2524-2552`); pong clears it (`end_busy_cursor`, 2565). Ping is (re)issued on activation/click (shell.c:1368,1448). No forced kill — purely cosmetic feedback.

*Copy hint:* keep a per-surface list of outstanding configure serials and discard-older/match-exact/reject-unknown on ack; validate geometry against the configured maximized/fullscreen size at commit; make "unresponsive" a UX state (busy cursor) not a kill.

## 4. Frame callbacks & repaint scheduling

Frame callbacks accumulate on a per-surface list `surface->frame_callback_list` (compositor.c:1202; request handler `surface_frame` at compositor.c:5099). The key throttle: callbacks are **only fired for surfaces that are actually on the output and not occluded**. In `weston_output_repaint` (compositor.c:4087-4114): it iterates the z-order paint nodes, skips any pnode whose `pnode->surface->output != output` or whose visible region is empty (`!pixman_region32_not_empty(&pnode->visible)`, 4100), splices those surfaces' callbacks into a local list, and only then sends `wl_callback_send_done` + destroys them. So a hidden/occluded/minimized client's frame callbacks never fire → it self-throttles to zero and cannot spin.

Repaint is a state machine `repaint_status` (`REPAINT_NOT_SCHEDULED / SCHEDULED / BEGIN_FROM_IDLE / AWAITING_COMPLETION / DEFERRED`, compositor.c:3912-4978). `weston_output_schedule_repaint` (4953) is idempotent — returns early if `repaint_status != REPAINT_NOT_SCHEDULED` (4978), so unlimited client commits collapse into at most one scheduled repaint. Actual repaints are paced by the output's vblank/`finish_frame`, not by client activity; an idle source (`idle_repaint`, 4726) drives it. On repaint failure it drops the frame and hopes for a later reschedule (4123-4131) rather than busy-looping.

*Copy hint:* fire frame callbacks only from the actual per-output repaint pass and only for visible surfaces; make schedule_repaint idempotent against a status enum so client commit floods coalesce.

## 5. Resource exhaustion / OOM policy

No compositor-level caps on client count, buffer count, or callback count — weston relies on libwayland + OS limits (RLIMIT_NOFILE etc.). No `MAX_CLIENTS`/`RLIMIT` code in libweston/frontend.

OOM discipline is pervasive: 82 `*_post_no_memory` call sites in `libweston/*.c`. Pattern is consistent — every `wl_resource_create`/`zalloc` failure in a request handler emits `wl_client_post_no_memory` / `wl_resource_post_no_memory` and bails (e.g. compositor.c:5107, 5508, 6219; create_surface err path 5504-5508; subsurface 6216,6224). Internal helper allocations use `xzalloc`/`xalloc` (`shared/xalloc.h`) which `abort()` on failure (xalloc.h:52) — deliberate crash for compositor-internal allocations vs. graceful protocol OOM for client-triggered ones. Color-mgmt notably chooses `zalloc`+`post_no_memory` for the up-to-32 MB ICC buffer specifically to avoid aborting on client-sized allocations (color-management.c:1066-1071).

The one explicit DoS acknowledgement about resource exhaustion: `libweston/color-management.c:1075` "TODO: it is not that simple. Clients can abuse that to DoS the compositor" (linking an MR discussion) — synchronous ICC read.

*Copy hint:* split allocation policy — `abort()` on compositor-internal OOM, but `post_no_memory` + graceful bail on any allocation whose size a client controls.

## 6. Input / grab edge cases

Focus-vs-destruction is handled with paired destroy listeners on both the focused **view** and the focused **resource** for every input device (`libweston/input.c`):
- pointer: `pointer_focus_view_destroyed` / `pointer_focus_resource_destroyed` both call `weston_pointer_clear_focus` (input.c:394-410).
- keyboard: `keyboard_focus_resource_destroyed` (414).
- touch: `touch_focus_view_destroyed` / `_resource_destroyed` (424-438).
- tablet tool: (444-458).
Listeners are `wl_list_remove`+re-init on every focus change (e.g. input.c:1481, 1600, 1696) so a surface dying mid-event drops focus cleanly instead of leaving a dangling pointer.

**Drag lifecycle** (`libweston/data-device.c`): `weston_drag` holds `data_source_listener` and `icon_destroy_listener` (data-device.c:43,48) so source/icon death during a drag is caught. Offer callbacks all re-validate the source is still the current one: `data_offer_accept`/`receive` check `offer->source && offer == offer->source->offer` (data-device.c:74,91) — a stale offer after source death is a silent no-op, not a crash.

**Input region**: `set_input_region` just copies the client region (compositor.c:5145-5158, infinite if NULL), but it is **clamped to surface bounds at commit** via `pixman_region32_intersect_rect(&surface->input, &state->input, 0,0, surface->width, surface->height)` (`libweston/surface-state.c:499`) — a client cannot claim input outside its own surface.

**Popup grab** button/timeout dismissal covered in §3.

*Copy hint:* register destroy listeners on *both* the focused view and its wl_resource for every seat capability, and always intersect the client's input region with the surface rectangle.

## 7. Color management (`libweston/color-management.c`)

ICC profile fd validation in `cm_creator_icc_set_icc_file` (color-management.c:972-1022): reject double-set → `ALREADY_SET`; length `0` or `> 32 MB` → `BAD_SIZE` (988); fd not readable (`O_WRONLY`) → `BAD_FD` (996); fd not seekable (`lseek(fd,0,SEEK_CUR)<0`) → `BAD_FD` (1002). Overflow: `do_length_and_offset_fit` checks `length > SIZE_MAX - offset` and off_t round-trip (1024-1043); `static_assert(UINT32_MAX <= SIZE_MAX)` (1012). **Reads with `pread` in a loop (color-management.c:1080-1110), not mmap** — deliberately avoids SIGBUS on client ICC files; short read (`pread==0`, read past EOF) → `OUT_OF_FILE` protocol error (1104); read error → graceful `send_failed(CAUSE_OPERATING_SYSTEM)` (1093). Invalid ICC content from LCMS → `send_failed`, *not* a protocol error, because "we can't tell if it is client's fault" (1118-1122). Protocol discipline errors: image-description not ready → `NOT_READY` (380); querying a description with no info → `NO_INFORMATION` (388); ops on inert objects → `_ERROR_INERT` (681,845); bad render intent → `RENDER_INTENT` (701); duplicate surface object → `SURFACE_EXISTS` (799).

*Copy hint:* read client ICC/data fds with looped `pread` (never mmap), cap size (32 MB), and distinguish "client's fault" (protocol error) from "invalid content" (graceful failed event).

## 8. Client process isolation / spawn hygiene

Spawn in `wet_client_launch` (`frontend/main.c:440-513`): `fork()`; child calls `cleanup_for_child_process()` (main.c:424-438) which `setsid()`s into a new session (so Ctrl-C to weston/gdb doesn't hit clients) and **unblocks all signals** (`sigfillset`+`sigprocmask(SIG_UNBLOCK)`, 436-437) so children don't inherit weston's signal mask. Child drops privileges: `seteuid(getuid())` and `_exit` if it fails (main.c:472-476) — never execs a client as the wrong euid. FDs are CLOEXEC by default; only explicitly-listed `no_cloexec_fds` are un-cloexec'd (os_fd_clear_cloexec, 478-485) — the wayland socket fd is passed deliberately, everything else auto-closes on exec. Uses `execve` with a curated `envp` (custom_env). All async-signal-safe `write()`+`_exit` in the child error paths.

Reaping: `SIGCHLD` via a wl event-loop signal source `sigchld_handler` (main.c:380-419), `waitpid(-1, WNOHANG)` loop, matches pid against `child_process_list` and calls `wet_process_destroy` → per-process `cleanup` callback (370-374). CLOEXEC everywhere else too: socketpairs (`SOCK_CLOEXEC`, os-compatibility.c:97), epoll (`EPOLL_CLOEXEC`, 123), pipes (`pipe2(O_CLOEXEC)`, clipboard.c:263, xwayland.c:139).

*Copy hint:* in the forked child, setsid + unblock all signals + drop euid to the real uid + rely on CLOEXEC (un-cloexec only the socket), and reap via a SIGCHLD event-loop source that maps pid→cleanup.

## 9. Test suite — client-abuse categories (`tests/`)

Dedicated abuse/edge tests (protocol-error assertions via `expect_protocol_error`; counts = number of error assertions):
- **`bad-buffer-test.c`** — deliberately crash-the-compositor: creates an shm buffer then `ftruncate`s the fd to 12 bytes so the compositor reads out-of-bounds and would hit SIGBUS (create_bad_shm_buffer, tests/bad-buffer-test.c:144-169); asserts compositor survives. Copies the *old* unsealed tmpfile path on purpose "so SIGBUS can still happen" (comment lines 22-27).
- **`color-management-protocol-test.c`** — 34 protocol-error cases (the heaviest; ICC/image-description discipline).
- **`subsurface-test.c`** — 14 cases (bad-surface, own-parent, ancestor cycles, role conflicts).
- **`viewporter-test.c`** — 8 cases (src/dst bad values, out-of-buffer).
- **`linux-explicit-synchronization-test.c`** — 6 (no-buffer/unsupported-buffer fences).
- **`commit-timing-test.c`** (4), **`color-representation-protocol-test.c`** (4), **`alpha-modifier-smoke-test.c`** (3).
- **`roles-test.c`** — role conflict both directions (sub↔wl_shell), 2 cases (tests/roles-test.c:78-159).
- **`single-pixel-buffer-test.c`**, **`xdg-shell-test.c`**, **`fifo-test.c`**, **`constraints-test.c`** — 2 each; **`touch-test.c`** 1.
- Others exercising robustness: `client-buffer-test`, `buffer-transforms-test`, `safe-signal-test` / `safe-signal-output-removal-test` (destruction during signal emit), `assert-test` (the weston_assert macros), `devices-test`, `event-test`.

*Copy hint:* build a test that truncates an shm fd under the compositor and asserts survival, plus a table-driven protocol-error suite per extension.

## 10. Defensive-coding policy

**assert vs protocol error philosophy** — sharply separated:
- **Protocol errors** (`wl_resource_post_error`) for anything a client can trigger: it disconnects that one client, compositor lives. Used everywhere in §2/§3/§6/§7.
- **`weston_assert_*`** (`shared/weston-assert.h:56-95`) for compositor-internal invariants — prints `file:line: Assertion a cmp b (val fmt) failed` and `abort()`s (weston-assert.h:39-50). Typed variants: `weston_assert_true/false`, `_ptr_eq/_ne/_null/_not_null`, `_f32_eq`, `_fn_` for custom comparators. It carries a `struct weston_compositor *` so a custom `custom_assert_fail_` can be swapped in (weston-assert.h:52-54) — the test harness overrides it (`tests/harness/weston-test-assert.h`) so an assert becomes a test failure, not a process abort. Plain `assert()` still used for "can't happen" reachability (e.g. xdg-shell.c:1857 `assert(0 && "not reached")`).
- **`xalloc`/`xzalloc`** (`shared/xalloc.h`) abort on OOM — reserved for internal allocs; client-sized allocs use `zalloc`+`post_no_memory` (see §5).

**Integer-overflow discipline**: explicit `(uint64_t)` promotion before multiply/add in dmabuf size checks (linux-dmabuf.c:228,236); `wl_fixed`/`INT32_MAX` guards and bounded `fixed_sum_gt` in viewport (compositor.c:5261-5271); `SIZE_MAX`/`off_t` round-trip + `static_assert` in color-mgmt (color-management.c:1012,1033-1040); string→int parsing via `safe_strtoint` with ERANGE/EINVAL handling (`shared/string-helpers.h:44,82`). No dedicated `__builtin_add_overflow` helpers — hand-rolled widened-type checks.

**Security-relevant comments**: `color-management.c:1075` explicit client-DoS TODO; `weston-log.c:537` runtime warning that the debug protocol "is a potential denial-of-service attack vector and information leak"; the dmabuf create_immed "kill the client" rationale (linux-dmabuf.c:320-329); frontend TLS-disable warnings. No CVE citations in-tree.

*Copy hint:* adopt the two-tier rule — `abort()`-style asserts for internal invariants (overridable in tests), protocol errors for anything client-reachable — and do overflow math by promoting to a wider type before the operation, never after.


---

## Appendix C: mutter

_Verbatim final message of the `mutter client-robustness study` research agent._

I now have thorough evidence across all areas. Here are my findings.

---

# mutter: edge-case handling & defenses against buggy/malicious Wayland clients

All paths are `/home/pg/monorepo/tmp/composers/mutter/...`. A recurring architectural fact: mutter is a **libwayland server**, so most "kill the client" enforcement is `wl_resource_post_error()` (sends a fatal protocol error and disconnects that one client) — a misbehaving client never brings down the compositor. Non-fatal client bugs are handled with `g_warning` + "work around it."

## 1. Buffer / shm / dmabuf validation

- **SIGBUS on shm is NOT handled by mutter.** There is no `sigaction(SIGBUS,...)` anywhere in the tree. mutter delegates to libwayland's `wl_shm_buffer_begin_access()` / `wl_shm_buffer_end_access()` which install the SIGBUS-catching handler and longjmp out if a client truncates its shm pool mid-read. mutter wraps every shm read in that pair: `src/wayland/meta-wayland-buffer.c:436-442` (attach) and `:741-789` (readback). `src/core/mutter.c:107-117` only ignores SIGPIPE/SIGXFSZ.
- **shm format/stride:** `shm_buffer_attach` rejects unknown formats with a `GError` (→ becomes a `WL_DISPLAY_ERROR_NO_MEMORY` at commit) `meta-wayland-buffer.c:397-403`. shm data is copied into a texture at attach time, so destroy-before-release is safe (see area 9).
- **dmabuf param validation is layered.** At `add`: plane index bounds `ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX` (`meta-wayland-dma-buf.c:784`), double-set plane `ERROR_PLANE_SET` (`:793`), mismatched per-plane modifier `ERROR_INVALID_FORMAT` (`:802`). Note offset/stride are *not* range-checked here — they're validated implicitly by the EGL/gbm import. At `create` (`:1151-1248`): reused params `ERROR_ALREADY_USED` (`:1168`), missing plane 0 / gaps in planes `ERROR_INCOMPLETE` (`:1182,:1192`), unknown flags `ERROR_INVALID_WL_BUFFER` (`:1204`).
- **EGL/import failure path is explicit and dual-mode:** `buffer_params_create_common` realizes+imports the texture immediately so it can give sync feedback. On failure: if `buffer_id==0` (deferred `create`) it sends `zwp_linux_buffer_params_v1_send_failed` (soft, client can retry); if immediate (`create_immed`) it kills the client with `ERROR_INVALID_WL_BUFFER` (`:1224-1240`). This is the canonical "fallible import → soft-fail vs. fatal" split.
- Copy hint: *Never trust shm reads — wrap them in a SIGBUS-guarded access window; for dmabuf, do a real trial import at buffer-create time and report success/failure back to the client instead of crashing at scanout.*

## 2. xdg-shell: serial / geometry / role / popup / positioner

- **Role conflict machinery** lives in `meta_wayland_surface_assign_role()` `meta-wayland-surface.c:193-269`: first assignment creates the role GObject; a *second* assignment of a **different** `GType` returns `FALSE` (`:254-257`), and every caller turns that FALSE into a protocol error (e.g. subsurface `WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE` `meta-wayland-subsurface.c:567`, DnD icon `WL_DATA_DEVICE_ERROR_ROLE` `meta-wayland-data-device.c:907`, xdg `XDG_WM_BASE_ERROR_ROLE` `meta-wayland-xdg-shell.c:1872,:2275`). Same-type re-assign just updates properties.
- **Buffer-before-role** rejected: assigning xdg role to a surface that already has a committed buffer → `XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE` (`meta-wayland-xdg-shell.c:2085`). "get_toplevel/get_popup before xdg_surface constructed" → `XDG_SURFACE_ERROR_NOT_CONSTRUCTED` (`:2250,:2385,:2395`).
- **ack_configure serial:** stored, not validated on receipt — `xdg_surface_ack_configure` only requires a pending state exists (`:1932-1949`), otherwise `INVALID_SURFACE_STATE`. The serial is carried in `MetaWaylandSurfaceState.acked_configure_serial` and consumed at commit to drive `finish_move_resize` (`:1037-1041`, `:1552-1554`). No error on stale/unknown serial — mutter is lenient here.
- **Window geometry validation is "warn and work around," not fatal:** a zero-area geometry after clamping logs `g_warning("Client provided invalid window geometry ... Working around.")` and recomputes a default union geometry (`:2034-2050`). min/max size sanity: `is_new_size_hints_valid` failing → `INVALID_SURFACE_STATE "Invalid min/max size"` (`:1074`).
- **Popup grab lifecycle:** grab requested via `xdg_popup_grab` records `(grab_seat, grab_serial)` into `setup`; grabbing after map → `XDG_POPUP_ERROR_INVALID_GRAB "tried to grab after popup was mapped"` (`:634`). At map, the serial is validated by `meta_wayland_seat_can_popup(seat, serial)` — **if it doesn't match a recorded button/grab serial the popup is silently dismissed with `popup_done` rather than grabbing** (`:1330-1334`). Destroying a non-topmost popup → `XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP` (`:1713`). `dismiss_invalid_popup` unwinds the whole popup stack top-down (`:1410-1443`). Popup parent must be topmost (`:1339-1345`) and non-null role (`INVALID_POPUP_PARENT` `:2318-2339`).
- **xdg_positioner validation** is strict: non-positive size → `XDG_POSITIONER_ERROR_INVALID_INPUT` (`:2564,:2585`), plus anchor-rect/gravity/constraint-adjustment checks (`:2609,:2626,:2649`).
- **Shell-client teardown** posts `XDG_WM_BASE_ERROR_DEFUNCT_SURFACES` if the wm_base is destroyed while it still has live surfaces/popups (`:1681,:2116`).
- Copy hint: *Make role assignment a one-shot typed operation that returns failure on type change, and funnel every "wrong object/state/serial" into a per-object protocol error; be lenient on geometry (clamp+warn) but strict on positioner inputs and popup-grab serials.*

## 3. Unresponsive clients (ping/pong, "not responding", force-quit)

- Generic ping is in `src/core/display.c`; `meta_display_ping_window` (`:1585`) refuses serial 0 (`:1597-1602`), dedups pending pings per window and detects serial reuse (`:1607-1627`), and arms a `g_timeout_add(check_alive_timeout,…)` (`:1632`). Timeout → `meta_display_ping_timeout` (`:1547`) marks the window not-alive.
- **Event-flood heuristic:** `meta_window_check_alive_on_event` pings on input and counts `events_during_ping`; exceeding `MAX_QUEUED_EVENTS` (**400**, `src/core/delete.c:25`) forces `set_alive(FALSE)` without waiting for the timeout (`delete.c:109-112`).
- **Dialog + force-quit flow:** `meta_window_set_alive(FALSE)` schedules `meta_window_ensure_close_dialog_timeout` → after another `check_alive_timeout` shows the close dialog (`window.c:7989-8014`). Dialog "Force Quit" → `close_dialog_response_cb` → `meta_window_kill` (`delete.c:41-42`). `set_alive(TRUE)` (pong arrives) cancels the timeout and hides the dialog (`window.c:8026-8029`).
- **Kill escalation:** `meta_window_kill` tries `kill(pid, SIGKILL)` first, falls back to the class `->kill` vfunc (X11 XKillClient / Wayland client destroy) (`delete.c:126-146`).
- xdg-shell ping is wired through `meta_wayland_xdg_surface_ping → xdg_wm_base_send_ping` (`meta-wayland-xdg-shell.c:2097-2106`) and pongs land in `meta_display_pong_for_serial` (`:2810-2818`).
- Copy hint: *Track pings by serial with dedup + a per-window liveness bool; escalate on both a timeout and an event-flood threshold, and gate the SIGKILL behind a user-visible dialog.*

## 4. Frame clock / callbacks / scanout gating

- **Transactions gate on buffer readiness and FIFO barriers**, not on frame acks. `src/wayland/meta-wayland-transaction.c`: a committed transaction only applies when all its buffers' fences are ready and it's first in the surface's queue; FIFO barrier: an entry with `fifo_wait` blocks until `surface->fifo_barrier` clears (`:266-285`). Clients that never commit simply never advance their own transaction chain — they don't stall others (per-surface chains, `surface->transaction.first_committed`). fifo protocol errors are surface-destroyed guards only (`meta-wayland-fifo.c:66,:86`).
- **commit-timing protocol** validates: timestamp already set `WP_COMMIT_TIMER_V1_ERROR_TIMESTAMP_EXISTS` (`meta-wayland-commit-timing.c:82`), invalid timestamp `ERROR_INVALID_TIMESTAMP` (`:90`), duplicate timer on surface `COMMIT_TIMER_EXISTS` (`:137`), surface destroyed (`:72`).
- **Direct-scanout validation before flipping a client buffer is extensive** — `find_scanout_candidate` (`src/compositor/meta-compositor-view-native.c:264-374`) bails out (compositing fallback) on: debug-disabled, overlapping software cursors (`:290`), non-KMS CRTC (`:301`), no onscreen FB (`:309`), shadowfb present (`:316`), surface color-state needs mapping (`:333`), unsupported color model/coeffs (`:359`), no surface. Then `meta_wayland_dma_buf_try_acquire_scanout` (`meta-wayland-dma-buf.c:647-745`) checks the client's DRM format+modifier against `crtc_supports_modifier`, imports a `gbm_bo`, builds a `MetaDrmBufferGbm`, and `meta_onscreen_native_is_buffer_scanout_compatible` (`meta-wayland-buffer.c:871`). shm/single-pixel buffers are never scanned out (`:908-912`).
- **Scanout-failure taint:** if a scanout is rejected at KMS commit, `on_scanout_failed` marks the (buffer,onscreen) pair in `buffer->tainted_scanout_onscreens` and future attempts skip scanout for that pair, forcing composition (`meta-wayland-buffer.c:553-560, 899-904`) — this prevents an endlessly-retried bad flip.
- Copy hint: *Treat direct scanout as an optimization guarded by a long list of "must be true" checks, do a real KMS-compatibility test, and remember buffers that failed to flip so you don't retry them into a black screen.*

## 5. Serial/timestamp validation for grabs, activation, focus stealing

- **Grab serials are validated against recorded input serials.** The pointer records `focus_serial`, `click_serial` (per button press, `meta-wayland-pointer.c:477-483`) and `grab_serial` (`:765`). `meta_wayland_pointer_can_grab_surface` requires the serial to equal `grab_serial` *or* `click_serial` and the surface to be the focus (`:1443-1447`). `can_popup` requires serial == `grab_serial` (`:1488-1490`). All move/resize/drag/popup entry points route through `meta_wayland_seat_get_grab_info` (`meta-wayland-seat.c:605`) which fans out to pointer/keyboard/touch/tablet — so a client can't fabricate a grab from a serial it never received.
- **xdg-activation token validation (mutter's Wayland focus-steal defense):** `token_can_activate` (`meta-wayland-activation.c:288-311`) requires the token to have a real `seat` + `surface` and a serial that passes `keyboard_can_grab_surface` or `seat_get_grab_info` — i.e. the requesting app must have had recent input focus. Tokens are single-use (`token->committed` → `"Activation token was already used"` `:163`), random UUID+time (`create_startup_token` `:134-149`). **If validation fails, the window does NOT get focus — it gets `meta_window_set_demands_attention` instead** (`maybe_activate` `:354-372`). For tokens that came via startup-notification (no live token object), `startup_sequence_is_recent` requires `seq_timestamp >= last_user_time` (`:313-322`) — an anti-replay/anti-stale check.
- **X11 focus-stealing prevention** (the classic mutter policy) in `src/core/window.c`: `intervening_user_event_occurred` (`:2003-2104`) compares the new window's `_NET_WM_USER_TIME`/startup `initial_timestamp` against the focus window's user time (`XSERVER_TIME_IS_BEFORE`, `:2087-2096`); `user_time==0` means "explicitly no focus" (`:2038-2047`). `window_state_on_map` (`:2109`) then applies STRICT-mode ("only focus if ancestor of current transient", `:2133-2142`) and refuses focus for utility/dock/menu/notification/etc. types (`:2144-2168`). `meta_display_sanity_check_timestamps` fixes up clients that send timestamps from the future (`display.c:2172-2211`).
- Copy hint: *Bind every grab/activation to a serial you actually handed the client, make activation tokens single-use and focus-gated, and when a window "isn't allowed" to focus, demote to attention-request rather than denying outright.*

## 6. Selection / clipboard / DnD hygiene

- **Cross-client clipboard theft blocked:** `data_device_set_selection` cancels the source and refuses if the requesting client isn't the current `focus_client` (`meta-wayland-data-device.c:1147-1152`). A DnD-actioned source can't be used as a clipboard selection → `WL_DATA_SOURCE_ERROR_INVALID_SOURCE` (`:1140`). Serial monotonicity guards out-of-order sets: `selection_serial - serial < UINT32_MAX/2` → ignore older (`:1088-1090`).
- **start_drag validation:** null origin → silent return (`:880`); grab-serial must validate via `seat_get_grab_info(require_pressed=TRUE)` (`:883-889`); only one grab at a time (`:893`); icon-surface role conflict → `WL_DATA_DEVICE_ERROR_ROLE` (`:907`). (Note an acknowledged FIXME: empty mime-type array isn't checked, `:891`.)
- **Source death handling:** weak refs everywhere — `selection_data_source_destroyed` clears the selection when the owning source object dies (`:926-932`); drag focus destroy listeners (`destroy_drag_focus`, `on_drag_focus_destroyed`, `:202-220`); `drag_grab_data_source_destroyed` tears down the drag. Clipboard clears on source death via `unset_selection_source`.
- **Clipboard manager (persist across source death):** `src/core/meta-clipboard-manager.c` — on owner change it snapshots the clipboard bytes for a preferred mimetype (`owner_changed_cb` `:88-146`); when the owner disappears entirely it *takes over ownership* with an in-memory source so a copied selection survives the app exiting (`:150-163`). fd/mime transfer flows through `MetaSelection`/`MetaSelectionSource`.
- **primary selection** is a parallel implementation (`meta-wayland-data-device-primary.c`, `-primary-legacy`) with the same source/offer/serial structure.
- Copy hint: *Gate selection-set on input focus + serial, keep weak refs so a dying source auto-clears state, and run a clipboard manager that assumes ownership on source death so copy-then-quit still works.*

## 7. Resource exhaustion / misc DoS

- **Damage is clamped** to surface and buffer rectangles before scaling to avoid integer overflow: `mtk_region_intersect_rectangle(surface_region, &surface_rect)` and `...(buffer_region, &buffer_rect)` in `meta-wayland-surface.c:332-388`, with an explicit comment "avoid integer overflow when scaling a damage region [that] is too [large]" (`:333`).
- **Subsurface cycle/depth defense:** `wl_subcompositor_get_subsurface` rejects `is_same_or_ancestor(surface, parent)` → `WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE "Circular relationship"` (`meta-wayland-subsurface.c:553-560`); double role → error (`:547`). `subsurface_place` validates the sibling is a real parent/sibling via `is_valid_sibling` → `WL_SUBSURFACE_ERROR_BAD_SURFACE` (`:387-394`). Tree is a `GNode`; there's no explicit depth cap, but cycles (the DoS vector) are impossible.
- **Buffer-scale integer constraint:** buffer dimensions must be an integer multiple of `buffer_scale` → `WL_SURFACE_ERROR_INVALID_SIZE` for normal surfaces; for cursor surfaces it's downgraded to a debug log ("Bug in client with pid…") to avoid killing over a cursor (`meta-wayland-surface.c:1194-1219`).
- **wl_surface offset** after the protocol version that forbids it → `WL_SURFACE_ERROR_INVALID_OFFSET` (`:1311`); invalid transform → `INVALID_TRANSFORM` (`:1473`); scale ≤ 0 → `INVALID_SCALE` (`:1494`).
- OOM on texture import → `WL_DISPLAY_ERROR_NO_MEMORY` (`:1170`).
- Region backing is `mtk_region` (pixman-backed), which itself caps rectangle counts; mutter just unions/intersects (`meta-wayland-region.c:52`). No explicit per-client global/resource count limits were found — mutter leans on libwayland's own object-id space and per-resource errors.
- Copy hint: *Clamp damage to the buffer before any scaling math, forbid subsurface cycles at attach time, and downgrade cosmetic violations (cursor size) to warnings instead of disconnects.*

## 8. Security: privileged-protocol gating & trust boundary

- **Global visibility filter** (`src/wayland/meta-wayland-filter-manager.c`): registered via `wl_display_set_global_filter`; per-global `MetaWaylandFilter` returns `META_WAYLAND_ACCESS_ALLOWED/DENIED` so restricted globals are invisible in the registry to unauthorized clients (`:35-56`).
- Only two protocols use it: **mutter-x11-interop** requires the client to hold `META_WAYLAND_CLIENT_CAPS_X11_INTEROP` (`meta-wayland-x11-interop.c:90-105`), and **Xwayland keyboard-grab** is allowed *only* for the Xwayland client itself (`xwayland_grab_keyboard_filter`: `client == xwayland_manager->client`, `meta-xwayland-grab-keyboard.c:335-347`). Xwayland grabs additionally honor allow/deny `xwayland-grab-access-rules` settings (`:196-225`).
- **Capabilities** are set explicitly on trusted subprocess clients: `meta_wayland_client_set_caps` / `has_caps` (`meta-wayland-client.c:296-306`), granted only to clients mutter itself launches (`meta_wayland_client_new_subprocess`, `:220`).
- **No wayland `security-context` / sandbox protocol and no direct screencast wayland protocol exist in this tree** — `grep` for `security_context`/`wp_security` finds nothing; there's no screen-capture wayland global. Screen capture is therefore portal/D-Bus-mediated only (RemoteDesktop/ScreenCast live outside `src/wayland/`). Custom protocols shipped are just `gtk-shell.xml`, `mutter-x11-interop.xml`, `xdg-session-management-v1.xml` (`src/wayland/protocol/`).
- **X11↔Wayland trust boundary** is drawn by treating Xwayland as one client with special caps; subsurfaces refuse X11 parents ("XWayland subsurfaces not currently supported" `meta-wayland-subsurface.c:576`).
- Copy hint: *Hide privileged globals with a registry filter keyed on per-client capabilities you only grant to processes you launched, and keep screen-capture out of the Wayland socket entirely (portal-gated).*

## 9. Crash / robustness

- **shm destroy-before-release** is safe because shm content is copied into a Cogl texture at attach (`multi_texture_from_shm`, `meta-wayland-buffer.c:437`); the `wl_buffer` resource can die immediately (tested by `shm-destroy-before-release.c` with ref images).
- **Buffer release accounting:** `use_count` with `inc/dec_use_count`; `wl_buffer_send_release` only fires at count 0, and explicit-sync release points (drm-syncobj) are signaled via `handle_release_points` (`meta-wayland-buffer.c:642-699`). `g_warn_if_fail(use_count == 0)` on finalize (`:957`).
- **Scanout taint** (area 4) recovers from a GPU/KMS rejecting a client buffer without a crash loop.
- **GPU/mode robustness:** `meta_renderer_native_queue_modes_reset` re-applies modes after device changes / hotplug (`src/backends/native/meta-renderer-native.c:730,1117,1671`); GPU add/remove handled in `src/backends/meta-gpu.c`. (Full GL-robustness/`GL_GUILTY` reset handling lives in Cogl, not here.)
- **Client crash cleanup** is GObject-lifecycle + `wl_resource` destroy listeners throughout (drag focus, buffer, surface `resource-destroyed` signals) so a client disconnect cascades cleanly; popup stacks unwind via `shell_client_destroyed` posting `DEFUNCT_SURFACES` then destroying resources.
- Copy hint: *Copy shm into your own texture at attach so buffer lifetime is decoupled from the client, and use refcounts + destroy-listeners so a client vanishing is just normal object teardown.*

## 10. Tests exercising abusive clients

Test harness: `src/tests/meta-wayland-test-utils.c` spawns each client as a `GSubprocess` and asserts `g_subprocess_get_successful` (`:59`) — a client expecting a protocol error catches its own `wl_display` error and exits 0. Abusive/edge-case client programs in `src/tests/wayland-test-clients/`:

- **Protocol-abuse:** `invalid-geometry.c`, `invalid-size-limits-on-map-client.c`, `invalid-subsurfaces.c`, `invalid-xdg-shell-actions.c`, `buffer-less-viewport-client.c`, `toplevel-reuse-surface.c`.
- **Activation/focus-steal:** `xdg-activation-no-serial.c`, `xdg-activation-before-mapped.c`, `xdg-activation-serial.c` (driven by `wayland-xdg-activation-tests.c`).
- **Buffer lifecycle:** `shm-destroy-before-release.c`, `single-pixel-buffer.c`, `dma-buf-scanout.c`, `buffer-transform.c` (+ `wayland-buffer-tests.c`, ref-images under `src/tests/ref-tests/wayland_buffer_*`).
- **Subsurface corner cases:** `subsurface-corner-cases.c`, `subsurface-parent-unmapped.c`, `subsurface-remap-toplevel.c`, `subsurface-reparenting.c` (`wayland-subsurface-tests.c`).
- **DnD edge cases:** `dnd-order.c`, `dnd-reuse-icon.c`, `touch-dnd-same-surface.c`.
- **Sizing/scaling limits:** `xdg-apply-limits.c`, `xdg-toplevel-bounds.c`, `fixed-size-client.c`, `stable-rounding.c`, `repeated-preferred-scale.c`, `surface-scale-client.c`.
- **Suite drivers of interest:** `wayland-xdg-shell-tests.c`, `wayland-global-filter-tests.c` (the capability/registry-filter machinery from area 8), `wayland-idle-inhibit-tests.c`, `xwayland-tests.c`, `wayland-x11-interop-tests.c`.
- ASAN is force-enabled for test clients (`ASAN_OPTIONS` set in `meta-wayland-test-utils.c:97`), so use-after-free in these abuse scenarios fails the suite.
- Copy hint: *Ship a subprocess-based conformance harness where each "bad client" is a real binary that expects to be disconnected, and run it under ASAN so protocol-error paths double as memory-safety tests.*


---

## Appendix D: kwin

_Verbatim final message of the `kwin client-robustness study` research agent._

I now have thorough coverage across all ten areas. Here are my findings.

---

# kwin: defensive handling of buggy/malicious Wayland clients

Repo: `/home/pg/monorepo/tmp/composers/kwin`. HEAD is `9ab77d7` (xdg_surface set_window_geometry now posts `error_invalid_size` instead of an unspecified code for non-positive dimensions — visible at `src/wayland/xdgshell.cpp:260-262`). `src/wayland/` is the merged-in KWaylandServer; `src/` is the compositor core.

## 1. Protocol validation discipline (post_error patterns)

The core pattern everywhere: server-side protocol handlers validate arguments and call `wl_resource_post_error(resource->handle, <enum>, "msg", ...)`, which kills the offending client connection. Density leaders: `xdgshell.cpp` (24), `colormanagement_v1.cpp` (21), `linuxdmabufv1clientbuffer.cpp` (17), `subcompositor.cpp` (12), `layershell_v1.cpp` (11).

What's validated where:
- **Role/state uniqueness** (double-assigning a role): `xdgshell.cpp:197,214` (`error_already_constructed`); `subcompositor.cpp:48`; `datadevice.cpp:97-99` (dnd_icon role). `fifo_v1.cpp:31` (second fifo per surface).
- **Role-before-use ordering**: `xdgshell.cpp:256` (`error_not_constructed` "xdg_surface must have a role"); attach-buffer-before-configure → `XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER` at `xdgshell.cpp:345,807`; `error_not_constructed` "surface has not been configured yet" gates move/resize/menu (`xdgshell.cpp:446,459,472`).
- **Lifetime ordering**: `xdgshell.cpp:73-74` xdg_wm_base destroyed before children → `error_defunct_surfaces`.
- **Sizes**: window geometry `width<1||height<1` → `error_invalid_size` (`:260`); `set_max_size`/`set_min_size` reject negatives (`:521,536`); min>max caught at commit-apply time (`:361,365`).
- **Enums** validated with explicit switch + default→error: resize edge (`:506`), anchor (`:995`), gravity (`:1048`), layer (`layershell_v1.cpp:116`), anchor/exclusive-edge (`layershell_v1.cpp:185,221`), buffer transform/scale (`surface.cpp:385,395`).
- **Serials**: ack_configure only stored if `pending` exists (`:268-275`); selection/primary-selection serials are checked **monotonic** — `updateSelection`/`updatePrimarySelection` ignore stale serials (`seat.cpp:308-327`, `if (serial < currentSelectionSerial) return`).
- **Subcompositor topology**: rejects self-parent (`subcompositor.cpp:56`) and ancestor cycles (`:60`) — prevents infinite recursion in surface trees.

Note the follow-up debt the HEAD commit flags: `plasmashell.cpp:90,95,165` still post raw `0` and `-1` error codes ("Invalid surface", "open_under_cursor: surface has a buffer").

Copy hint: adopt a strict "validate-then-`post_error`-with-the-protocol's-own-enum" discipline on every request handler; treat any invariant break as fatal to that client, not a silent clamp.

## 2. Buffer handling

- **shm SIGBUS defense** (`shmclientbuffer.cpp`): a client can `ftruncate` its pool smaller after sharing; touching now-missing pages would SIGBUS the compositor. kwin installs a `SA_SIGINFO|SA_NODEFER` SIGBUS handler once (`std::call_once`, `:305-311`) that walks a lock-free intrusive list of currently-mapped buffers (`s_accessedBuffers`, atomic) and, on a fault within a known mapping, replaces it with a zero-filled `MAP_ANONYMOUS|MAP_FIXED` region (`:290-296`); otherwise re-raises the previous handler. Optimization: if the fd has `F_SEAL_SHRINK` and is already large enough, `sigbusImpossible` skips the whole mechanism (`:71-78`).
- **shm size sanity** (`shm_pool_create_buffer`, `:105-133`): format must be in allowlist `s_formats`; then `offset<0 || width<=0 || height<=0 || stride<width || INT32_MAX/stride<height || offset > size - stride*height` → `WL_SHM_ERROR_INVALID_STRIDE` — explicit overflow-safe arithmetic. Also stride must be a multiple of bytes-per-pixel (`:128`). Pool resize can't shrink (`:152`) and remap failure is fatal (`:160`).
- **dmabuf import validation** (`linuxdmabufv1clientbuffer.cpp`): params object single-use (`error_already_used`, `:129,153,204`); plane index bounds (`error_plane_idx`, `:134`); no double-set plane (`:139`); `LinuxDmaBufParamsV1::test()` (`:245-308`) checks: at least one plane, no holes in plane set, non-zero dims, per-plane `offset+pitch` overflow vs `UINT32_MAX`, `offset+pitch*height` overflow, and against real fd size via `lseek(SEEK_END)` (skipped gracefully if seek unsupported — kernel may not support it), plus consistent modifier across planes. Import failure → `error_invalid_wl_buffer`; unsupported flags rejected.
- **GraphicsBuffer allocator abstraction**: `GraphicsBuffer::Map`/`MapFlags` abstraction (`shmclientbuffer.cpp:298`) with refcounted `map()/unmap()`; `ShmPool` is manually refcounted (`ref()/unref()`, delete at 0) so a buffer outliving its pool destroy request is safe.

Copy hint: install a scoped SIGBUS handler around shm access and swap faulting pages for zeroed anonymous memory; do all buffer-geometry math with explicit `INT_MAX/stride` overflow guards and cross-check offsets against the real fd size.

## 3. Explicit sync / fifo / commit-timing

- **linux-drm-syncobj** (`linux_drm_syncobj_v1.cpp`): timeline import failure → `error_invalid_timeline` (`:54`); surface-gone → `error_no_surface` (`:116,134`). `maybeEmitProtocolErrors()` (`:157-186`) enforces the sync contract atomically at commit: buffer-with-sync must have both acquire and release points (`error_no_acquire_point`/`error_no_release_point`); acquire and release on the same timeline with `acquire >= release` → `error_conflicting_points` (deadlock prevention); sync only allowed on dmabuf buffers (`error_unsupported_buffer`). Note it does NOT block the compositor on the fence at commit time — points are validated then handed to the DRM commit path.
- **fifo** (`fifo_v1.cpp`): `set_barrier`/`wait_barrier` after surface destruction → `error_surface_destroyed` (`:64,74`); only one fifo per surface (`:31`). Barrier/wait flags flow into `SurfaceState` (`pending->fifoBarrier`, `hasFifoWaitCondition`) rather than blocking.
- **GPU fence stuck handling** lives on the render side — see GPU reset (§9), where `checkGraphicsResetStatus` is polled with a 10s bounded wait loop rather than blocking forever.

Copy hint: validate the entire explicit-sync contract (both points present, acquire<release on shared timeline, dmabuf-only) at commit as protocol errors; represent fifo barriers as commit state, never a blocking wait on the client thread.

## 4. xdg-shell configure/ack/ping/popup/positioner

- **Ping / "application not responding"**: `XdgShellInterfacePrivate::registerPing` (`xdgshell.cpp:42-57`) runs a single QTimer at `pingTimeout/2` twice: first fire emits `pingDelayed`, second emits `pingTimeout` and drops the ping. `XdgToplevelWindow` (`xdgshellwindow.cpp:545-548,1176-1210`) maps serials→`PingReason`; `handlePingDelayed` marks the window `unresponsive`; `handlePingTimeout` on a CloseWindow-reason ping spawns a `KillPrompt` (`:1185-1189`). Pong cancels the timer and clears unresponsive state (`:1205-1210`).
- **KillPrompt** (`killprompt.cpp`): launches an external `KWIN_KILLER_BIN` helper process (privilege/UI isolation), passing wid/appId/hostname/platform; Wayland windows are exported via xdg-foreign to give the killer a handle (`:76-80`). Handles remote X11 clients via `clientMachine()->hostName()`.
- **ack_configure serial machinery**: server just records `pending->acknowledgedConfigure` (`xdgshell.cpp:275`). The window keeps a queue `m_configureEvents`; on commit it pops all configures with `serial <= lastAcked` and keeps the newest as `m_lastAcknowledgedConfigure` (`xdgshellwindow.cpp:150-158`) — tolerant of a client acking an older serial, and only applies state once buffer is attached and surface configured.
- **Popup grab edge cases**: grab on an already-mapped surface → `error_invalid_grab` (`:847`). Popup with no parent → `error_invalid_popup_parent` (`:798`). Known gap explicitly TODO'd: destroying a non-topmost popup should post `NOT_THE_TOPMOST_POPUP` but currently just destroys (`:824-828`).
- **Positioner constraint solving** (`XdgPositioner::placement`, `:1218-1338`): implements the full spec order per-axis — try unconstrained; if out of bounds apply flip (XOR anchor+gravity edges, but only commit the flip if the flipped rect actually fits), then slide (clamp to bounds edge), then resize (shrink to bounds, only if result stays valid). Handles fractional parent/popup scale (`scaled(1.0/m_parentScale)`). Positioner itself validates non-zero size (`:977,986`) and known anchor/gravity enums at set-time.
- Repositioning tests exist including `testXdgPopupRepositionBeforeInitialCommit` and `testXdgPopupReactive`.

Copy hint: run ping on a two-phase timer (delayed→mark unresponsive, timeout→offer kill via a separate helper process), and make ack_configure a "pop everything ≤ acked serial" queue so clients acking stale serials don't wedge you.

## 5. Window size sanity / rules

- **min/max resolution** (`xdgshellwindow.cpp:627-637`): `minSize()` runs the client value through window `rules()->checkMinSize` then `expandedTo` an enforced floor of **20px** (150px with SSD decoration) — so a client asking 0x0 still gets a usable window. `maxSize()` maps client 0 → `INT_MAX` (0 means unlimited per spec), applies `checkMaxSize` rule, then `expandedTo(minSize())` guaranteeing max≥min.
- **frame-size clamping** (`window.cpp:4220-4232` `constrainFrameSize`, and `:4223-4224`): `std::clamp(width, min, max)`; used on every geometry path (`:1621,4182`, placement.cpp uses `constrainFrameSize` ~10 places). Move/resize edge handling clamps against min/max frame sizes per-edge (`window.cpp:1477-1517`).
- Server also rejects min>max at commit (`xdgshell.cpp:361-365`) so client-side inconsistency is a protocol error, while compositor-side rule interaction is clamped. `isResizable()` returns false when min==max (`:704`).
- `opacity` clamped to `[0,1]` (`window.cpp:189`).

Copy hint: never trust client min/max — enforce a hard minimum floor, treat 0 as "unlimited"→INT_MAX, and `expandedTo(min)` the max so the invariant max≥min always holds regardless of client input.

## 6. Input: grabs, constraints, shortcuts-inhibit, fake-input

- **Drag grab lifecycle vs death**: `startDrag` connects `dragSource → aboutToBeDestroyed → cancelDrag` (`seat.cpp:1289-1294`); drag origin surface death also tears down (`datadevice.cpp:285`). Drag is validated against the implicit grab serial: on the matching button release only if `dragImplicitGrabSerial == currentButtonSerial` does it end the drag (`seat.cpp:684-693`) — a client can't hijack a drag with a stale/forged serial. Only one drag at a time (`startDrag` returns false if `drag.mode != None`, `:1278`).
- **Pointer constraints** (`pointerconstraints_v1.cpp`): validates pointer/surface objects (`WL_DISPLAY_ERROR_INVALID_OBJECT`, `:40,46`), rejects double-constrain on one surface (`error_already_constrained`, `:51,95`), unknown lifetime enum (`:56,90`). Lifecycle test coverage: `testCloseWindowWithLockedPointer` (`pointer_constraints_test.cpp:336`) exercises surface death under an active lock.
- **Keyboard-shortcuts-inhibit** (`keyboard_shortcuts_inhibit_v1.cpp`): inhibitors keyed by `(surface,seat)` pair to prevent duplicates; each has an `m_active` flag the compositor controls (trust stays with the compositor — the client requests, kwin decides activation). Restricted at the interface-filter level is not applied, but activation is compositor-gated.
- **Fake-input** (`backends/fakeinput/fakeinputbackend.cpp`): every event handler early-returns unless `device->isAuthenticated()` (`:130` and ~11 sites). BUT `authenticate` currently just calls `setAuthenticated(true)` with a `// TODO: make secure` (`:119-123`) — so real protection comes from the **global interface filter** (§7): `org_kde_kwin_fake_input` is in `restrictedInterfaces`, so sandboxed clients can't even bind it. Button/key state is also de-duplicated (pressing an already-pressed button returns, `:147-152`) and on device teardown all pressed buttons/keys/touches are synthetically released (`:104-115`) to avoid stuck input.

Copy hint: bind every drag/grab to `aboutToBeDestroyed` cleanup and gate drag-end on the exact implicit-grab serial; for privileged fake-input, rely on connection-level interface filtering rather than an in-band "authenticate" request.

## 7. Privileged protocol gating

- **Mechanism**: `FilteredDisplay` (`wayland/filtered_display.cpp`) installs `wl_display_set_global_filter`; the callback resolves the `wl_client`→`ClientConnection` and calls virtual `allowInterface(client, name)`.
- **Policy** (`wayland_server.cpp:127-160`): a hardcoded `restrictedInterfaces` set — `org_kde_plasma_window_management`, `org_kde_kwin_fake_input`, `zkde_screencast_unstable_v1` (screencopy/screencast), `org_kde_plasma_activation_feedback`, `kde_lockscreen_overlay_v1`, `wp_security_context_manager_v1`. Rules: xwayland-only interfaces (`zwp_xwayland_keyboard_grab_manager_v1`, `xwayland_shell_v1`) allowed only for the xwayland connection; input-method interfaces only for the input-method connection; **sandboxed clients are denied all restrictedInterfaces** (`:154-156`). Note the manager interface itself (`wp_security_context_manager_v1`) is restricted from sandboxed clients — a sandbox can't create sub-sandboxes.
- **Sandbox detection** (`clientconnection.cpp:31-57`): via systemd — `sd_pid_get_user_slice(pid) == "app.slice"` AND user-unit prefixed `app-flatpak-` or `snap.`. Determined once at connection from `wl_client_get_credentials` pid.
- **SecurityContext** (Wayland `wp_security_context_v1`, `securitycontext_v1.cpp` + `display.cpp:245-297`): client passes a `listen_fd`+`close_fd`; metadata (app_id, sandbox_engine) must be non-empty and set before a single `commit` (`error_already_used`/`error_invalid_metadata`). On commit a `SecurityContext` object listens on `listen_fd`; every `accept4`ed client is tagged with the app_id via `setSecurityContextAppId` (`display.cpp:285`); lifetime bound to `close_fd` closure. This lets a sandbox manager pre-restrict a nested socket.

Copy hint: gate privileged globals with a display-level global filter keyed on a robust sandbox check (cgroup/systemd slice, not just app_id), and implement wp_security_context so sandboxes hand you a pre-tagged listening socket.

## 8. Selection / DnD edge cases

- **Source death mid-drag**: `dragSource → aboutToBeDestroyed → cancelDrag` (`seat.cpp:1289`); origin surface death also cancels (`datadevice.cpp:285`).
- **Selection source type confusion**: `set_selection` rejects a source that was already used for drag-and-drop (`error_invalid_source`, `datadevice.cpp:118,123`).
- **Serial monotonicity**: stale selection/primary-selection serials ignored (`seat.cpp:310,321`), preventing an old racing client from clobbering a newer clipboard owner.
- **DnD icon role**: enforced to `dnd_icon`, error if surface already has another role (`datadevice.cpp:96-103`); icon lifetime tied to surface.
- **Data-control** (`datacontroldevice_v1.cpp`, wlr-data-control for clipboard managers like klipper) has its own post_error validation (2 sites) and is how KDE's clipboard history integrates.
- Tests: `dnd_test.cpp`, `selection_test.cpp`, plus xwayland variants `xwayland_dnd_test.cpp`/`xwayland_selection_test.cpp`.

Copy hint: make clipboard/primary "set" idempotently ignore stale serials, tie DnD teardown to source-and-origin `aboutToBeDestroyed`, and reject cross-purpose (drag vs selection) source reuse.

## 9. Unresponsive/hung clients & GPU resilience

- **GPU reset recovery** (`opengl/eglbackend.cpp:44-86`, driven from `compositor.cpp:747`): each frame, `checkGraphicsReset()` calls `glGetGraphicsResetStatus` (`eglcontext.cpp:502-506`, robustness extension). On GUILTY/INNOCENT/UNKNOWN reset it logs, then **bounded-waits up to 10s** (`while(timer.elapsed()<10000 && status!=NO_ERROR) usleep(50)`) for the driver to finish resetting, then returns true → `Compositor::composite` calls `reinitialize()` and fires a `graphicsreset` KNotification ("Desktop effects were restarted due to a graphics reset"). If `makeCurrent` fails it assumes reset and rebuilds everything.
- **Compositor restart resilience** tested by `dont_crash_reinitialize_compositor.cpp`.
- **Unresponsive clients**: the ping/KillPrompt path in §4; `killWindow()` (SIGKILL path) and interactive kill via `killwindow.cpp` (excludes plasmashell from force-close, `:34`).
- **Connection buffer cap**: `setDefaultMaxBufferSize` defaults to 1 MiB (`wayland_server.cpp:167-181`, overridable via `KWIN_WAYLAND_DEFAULT_MAX_CONNECTION_BUFFER_SIZE`) — caps per-client libwayland buffer so a client can't OOM the compositor by flooding requests.
- **xwayland crash/restart**: `xwaylandserver_crash_test.cpp`, `xwaylandserver_restart_test.cpp`.

Copy hint: poll GL robustness status per-frame with a bounded reset-wait then full re-init, and set libwayland's per-client max buffer size so a request-flood can't exhaust memory.

## 10. Tests: abusive-client coverage categories

`autotests/integration/` categories relevant to hostile/edge behavior:
- **"don't crash" regression suite**: `dont_crash_{aurorae_destroy_deco,cancel_animation,empty_deco,glxgears,reinitialize_compositor,useractions_menu}.cpp` — each reproduces a historical crash from odd client/decoration/lifecycle states.
- **Unresponsive/kill**: `xdgshellwindow_test.cpp:846 testUnresponsiveWindow` — launches an external `kill` helper, `SIGUSR1`-freezes it, verifies the two-stage timeout (unresponsive at killPingTimeout/2, then kill) with fuzzy cross-process timing.
- **Privilege/sandbox**: `security_context_test.cpp` (`testSecurityContext`, `testClosedCloseFdOnStartup`), `fakeinput_test.cpp`, `input_capture_test.cpp`, `no_global_shortcuts_test.cpp`.
- **Lifecycle-under-grab**: `pointer_constraints_test.cpp` (`testCloseWindowWithLockedPointer`), `move_resize_window_test.cpp` (`testDestroyMoveClient`, `testDestroyResizeClient` — client dying mid-interactive-op).
- **Geometry/state edge**: `xdgshellwindow_test.cpp` (`testXdgNeverCommitted`, `testXdgWindowGeometryIsntSet`, `testXdgPopupRepositionBeforeInitialCommit`, `testDoubleMaximize`, `testDoubleFullscreenSeparatedByCommit`, `testReentrantSetFrameGeometry`, `testRestoreWithNewMinSize`).
- **Buffer/sync/protocol**: `test_fifo.cpp`, `tearing_control_test.cpp`, `alpha_modifier_test.cpp`, `test_subsurface.cpp`, `test_colormanagement.cpp` (one of only two using explicit protocol-error assertions), plus `keymap_creation_failure_test.cpp` (bad xkb keymap from client).
- **xwayland hostility**: crash/restart/dnd/selection/input variants.

Copy hint: seed your test suite with a permanent "don't crash" bucket (one test per historical crash) plus explicit "client dies mid-{drag,move,resize,lock}" and "never-committed / double-state-change" lifecycle cases — these are exactly the categories that catch compositor-side use-after-free.

---

Key files to mine directly: `src/wayland/xdgshell.cpp` (validation + ping + positioner), `src/wayland/shmclientbuffer.cpp` (SIGBUS), `src/wayland/linuxdmabufv1clientbuffer.cpp` (import checks), `src/wayland/seat.cpp` (drag/serial), `src/wayland/securitycontext_v1.cpp` + `src/wayland/display.cpp` + `src/wayland_server.cpp:127-160` (privilege gating), `src/opengl/eglbackend.cpp` + `src/compositor.cpp:747` (GPU reset), `src/xdgshellwindow.cpp` + `src/killprompt.cpp` (unresponsive/kill).


---

## Appendix E: Hyprland + smithay

_Verbatim final message of the `Hyprland+smithay robustness study` research agent._

I have thorough coverage of both codebases. Here is my report.

---

# A) Hyprland (C++ / own protocol impl + aquamarine)

## 1. Protocol validation & resource-lifetime discipline

**Uniform WP-wrapper resource pattern** (`hyprwayland-scanner`-generated `CWp*`/`CXdg*` classes wrapped in hand-written `C*Resource` objects). Every protocol file follows the same defensive shape, e.g. `src/protocols/Viewporter.cpp:6-153`, `SecurityContext.cpp`, `DRMSyncobj.cpp`:
- Constructor immediately does `if UNLIKELY (!good()) return;` where `good()` is just `m_resource->resource()` (checks the wl_resource actually allocated) — `Viewporter.cpp:105-107`.
- Both `setDestroy` (client request) **and** `setOnDestroy` (wl destruction) are wired to the *same* `PROTO::x->destroyResource(this)` so the object is reaped on either path — `Viewporter.cpp:10-11`, everywhere.
- `destroyResource` is always `std::erase_if(container, [&](auto& o){ return o.get()==resource; })` — owner container holds `SP<>`, destruction is deferred/centralized, never `delete this` — `Viewporter.cpp:146-152`.
- Resources are looked up via static `fromResource(wl_resource*)` that returns a **locked weak-ptr** (`data ? data->m_self.lock() : nullptr`) so a stale resource yields `nullptr` rather than a dangling pointer — `core/Compositor.cpp:287-290`.
- Memory model is Hyprutils `CSharedPointer`/`CWeakPointer`/`CUniquePointer` (`SP/WP/UP`, `src/helpers/memory/Memory.hpp:9-15`); cross-object refs are `WP<>` and re-checked with `.lock()`/`.expired()` before use (e.g. `core/Compositor.cpp:389,418,462` prune expired subsurfaces).

**Error-posting discipline**: 202 `error()`/`postError` call sites in `src/protocols/`. Guard-then-return is standard, always returning after posting so no further mutation happens on a doomed resource. Representative:
- Viewport: `NO_SURFACE` when surface gone, `BAD_SIZE` for `<=0`, `BAD_VALUE` when source box doesn't fit (with a `wl_fixed` tolerance of 1/256 before rejecting) — `Viewporter.cpp:14-92`.
- Null-resource guards throughout: `RESOURCE_OR_BAIL` macro casts user-data and bails if null — `WaylandProtocol.hpp:9-12`.

*C++ copy hint:* one generated typed wrapper per interface + a `good()` gate + dual destroy hooks + centralized `erase_if` reaping + weak-ptr `fromResource` gives you null-safety and lifetime safety uniformly, so individual request handlers never re-check "is this alive."

## 2. Crash handling

- **Signal handlers**: `SIGSEGV`/`SIGABRT` → `handleUnrecoverableSignal` (skippable via `HYPRLAND_NO_CRASHREPORTER`), `SIGTERM` → clean crit handler, `SIGUSR1` user signal — `Compositor.cpp:288-295`.
- **Async-signal-safe crash reporter**: `src/debug/crash/CrashReporter.cpp` explicitly notes "we're in the signal handler, so we *only* have stack memory" and destroys scopes ASAP (`:53-71`). It re-implements signal-safe `getenv`/`strsignal` (`SignalSafe.cpp:16-34`) and a fixed-size `CMaxLengthCString<255>` because `<random>`, `perror`, `std::format` are unsafe; random splash message uses `time(NULL)%N` instead of `<random>` (`:40-43`). `exitWithError` writes with raw `write()` then `abort()` (`:45-51`). It walks loaded objects via `link.h`/`dl_iterate_phdr` and records loaded plugins (`PluginSystem.hpp` include).
- **Watchdog**: systemd sd_notify-style — `setWatchdogFd`/`writeWatchdogFd` (`Compositor.cpp:128-138`, `Compositor.hpp:67-68,105`) with `FD_CLOEXEC`.
- No git-history CVE lessons recoverable (repo is a depth-1 shallow clone; `git log` = 1 commit).

*C++ copy hint:* treat the crash path as a hostile environment — pre-size all buffers, reimplement the handful of libc calls you need signal-safe, and record plugin/DSO state for triage.

## 3. Buffer / dmabuf / explicit-sync validation

**LinuxDMABUF** (`src/protocols/LinuxDMABUF.cpp`) — full param validation before import:
- `plane > 3` → `PLANE_IDX`; plane already set → `PLANE_IDX`; already-used params → `ALREADY_USED` (`:150-162`).
- v5 mismatched modifier across planes → `INVALID_FORMAT` (`:169-170`).
- format+modifier not in supported set → `INVALID_FORMAT` (`:191-195`).
- On create: no planes → `INCOMPLETE`; missing plane 0 → `INCOMPLETE`; gap in planes → `INVALID_FORMAT`; dims `<1` → `INVALID_DIMENSIONS` (`:285-311`).
- **Integer-overflow guard**: `offset + stride*height > UINT32_MAX` → `OUT_OF_BOUNDS` (`:313-321`).
- Skips implosion-prone `DRM_FORMAT_MOD_INVALID`/`LINEAR` on scanout tranches (`:75-77`); optional `skip_non_kms_dmabuf_formats` quirk.

**Explicit sync** (`DRMSyncobj.cpp`) — validated at commit via `stateCommit` listener: missing buffer → `NO_BUFFER`; missing acquire/release timeline → `NO_ACQUIRE_POINT`/`NO_RELEASE_POINT`; acquire≥release on same timeline → `CONFLICTING_POINTS`; surface already has syncobj → `SURFACE_EXISTS`; timeline import failure → `INVALID_TIMELINE` (`:57-134,168-169`). State is locked with `LOCK_REASON_FENCE` until the fence signals; `createSyncRelease` warns if a releaser is taken twice (`:22-28`). DMABUF buffers without explicit acquire fall back to `exportSyncFiles()` implicit fencing in the commit path (`core/Compositor.cpp:171-175`).

**shm** (`core/Shm.cpp`): resize-shrink illegal (`:129`); size-vs-pool bounds and stride checks → `INVALID_STRIDE`/`INVALID_FD` (`:118,152,157,174`); every access checks `m_data != MAP_FAILED` (`:19,74,141,173`). (No custom SIGBUS jail — relies on client not truncating; contrast smithay below.)

*C++ copy hint:* validate dmabuf entirely on the manager side (plane count, gaps, modifier consistency, and especially the `offset+stride*height` overflow) and post the *exact* spec error enum; defer surface commit behind a fence lock rather than blocking the loop.

## 4. Popup / grab / focus / serial / ANR

- **Serial validation**: `xdg_toplevel.move`/`resize` ignored unless `g_pSeatManager->pointerButtonSerialValid(seat, serial, surf)` (`XDGShell.cpp:202-203,227-228`); invalid resize edge → `INVALID_RESIZE_EDGE` (`:215`); `ack_configure` monotonic — ignores serials `< m_lastConfigureSerial` (`:601-605`).
- **Initial-commit rule**: buffer attached before first commit → error (`XDGShell.cpp:523-524`); role reassignment guarded (`:855-856`, and `Subcompositor.cpp:169`, `LayerShell.cpp:242`).
- **Positioner**: size `<=0` and anchor-rect `w/h<=0` → `INVALID_INPUT` (`XDGShell.cpp:654-669`).
- **Subsurface cycles**: `surf==parent` → `BAD_SURFACE`; resolves the "t1 parent" (top ancestor) and rejects `t1Parent==surf` → `BAD_PARENT`; place-above/below on non-sibling → error (`Subcompositor.cpp:164-185,30-32,60-62`).
- **Popup grab**: a single shared `CSeatGrab` (kbd+pointer) with nested-popup stack; loses grab → callback dismisses all grabbed popups (`XDGShell.cpp:79-80,897-975`). `FocusGrab.cpp` (hyprland-specific) tracks per-surface destroy listeners that auto-erase on surface death (`:11-13`).
- **ANR (unresponsive-client) handling** — `src/managers/ANRManager.cpp`: a 1.5s timer pings each client's xdg_wm_base/xwayland surface; after `misc:anr_missed_pings` missed pongs it tints the window and pops a `hyprland-dialog` offering Terminate (`SIGKILL` the client PID) or Wait (`:60-113,208-222`). Gated on `hyprland-dialog` existing in PATH (`:18-22`) and `misc:enable_anr_dialog`. Dialog is spawned onto the client's own workspace; killed automatically on pong/close.

*C++ copy hint:* copy the ANR ping/kill loop verbatim (per-client ping timer + missed-count threshold + user dialog that SIGKILLs), plus monotonic `ack_configure` and mandatory pointer-button-serial validation for interactive move/resize.

## 5. DoS / privilege gating / security-context

- **security_context (`SecurityContext.cpp`)**: sandboxed clients created via `wl_client_create` on the passed listen-fd; all fds forced `FD_CLOEXEC` (`:36,57-58`); engine/appid/instance are write-once (`ALREADY_SET`) and locked after commit (`ALREADY_USED`) (`:71-114`); context outlives its resource until fd hangup (`:60-67,142-174`). `isClientSandboxed()` (`:221-223`) drives the global filter.
- **Privileged-global gating**: `wl_display_set_global_filter(::filterGlobals)` (`Compositor.cpp:284`). `filterGlobals` returns true (visible) for non-sandboxed clients; for sandboxed clients it hides any global **not** on a static *allow-whitelist* (`ProtocolManager.cpp:348-...`), with output globals allowed dynamically. So hyprland-private/privileged protocols (screencopy, global-shortcuts, toplevel-export, gamma, CTM, data-control, etc.) are simply invisible to sandboxed apps — allowlist not denylist (`Compositor.cpp:263-268`).
- **DynamicPermissionManager** (`src/managers/permissions/`): runtime per-client permission gates for SCREENCOPY, KEYBOARD (keylogging via virtual-kbd/global-shortcuts), CURSOR_POS, INPUT_CAPTURE, PLUGIN — modes DENY/ASK/ALLOW/PENDING; ASK pops an async dialog and returns a promise; rules sourced from config (binary-path RE2 regex) or runtime-user, keyed by `wl_client*` or pid+string (`DynamicPermissionManager.hpp:16-107`). `removeRulesForClient` on disconnect.

*C++ copy hint:* an **allow-whitelist** global filter keyed on security-context sandbox status (not per-protocol ad-hoc checks) plus a separate runtime "ask the user" permission layer for capture/input protocols is the clean two-tier model.

---

# B) smithay (Rust compositor library)

## 1. Typestate / ownership making edge cases unrepresentable

- **Role system** (`src/wayland/compositor/tree.rs`): role is `Option<&'static str>` on `PrivateSurfaceData`; `set_role` returns `Err(AlreadyHasRole)` if a *different* role is already set (idempotent for same role) — `:170-177,68-82`. All role assignment funnels through this, so "two roles on one surface" cannot silently happen.
- **Subsurface cycles are structurally rejected**: `set_parent` checks `child==parent || is_ancestor(child,parent)` → `Err(AlreadyHasRole)`, also rejects a surface that already has a parent — `tree.rs:357-386,338-351`.
- **Double-buffered/pending state via `MultiCache`/`Cached` state** (`compositor/cache.rs`, `mod.rs`): pending vs current is a first-class `cached_state.get::<T>().pending()/current()`; commit atomically promotes.
- **Pre/post-commit + destruction hooks** (`tree.rs:212-239`): protocols (fifo, xdg, syncobj) install `add_pre_commit_hook` closures rather than editing core commit code — hooks run with the surface lock dropped (`cleanup` clones hooks then `drop`s guard before invoking, `:157-163`) to avoid re-entrant deadlock.
- **Transaction model** (`compositor/transaction.rs`) with `Blocker`/`BlockerState` gates commit application until fences clear.

*C++ copy hint:* model role as a set-once field with an explicit `AlreadyHasRole` error, route *every* role/parent mutation through one guarded function that also does the ancestor-cycle check, and expose pre/post-commit hook lists so extension protocols never touch core commit logic.

## 2. Handler validation

- **wl_surface**: non-zero attach offset on v≥5 → `InvalidOffset`; scale must be positive → `InvalidScale` (`compositor/handlers.rs:184-188,301`). Subsurface place-above/below on non-sibling → `BadSurface` (`:583-597`); surface already has role in `get_subsurface` → `BadSurface` (`:457`).
- **xdg serial**: `ack_configure` searches `pending_configures` for exact serial; not found → returns `None` → caller posts `xdg_wm_base::InvalidSurfaceState "wrong configure serial"`; acking prunes all older configures and records `last_acked` (`shell/xdg/mod.rs:219-238`, `handlers/surface.rs:294-334`). Missing role on geometry/ack → `NotConstructed`; wrong role → `Role`; window geometry `w/h<=0` → `InvalidSize` (`handlers/surface.rs:239-260,274-280`).
- **Initial-configure invariant enforced in a pre-commit hook** (`xdg/mod.rs:1600-1666` toplevel, `1979-2066` popup): attaching a buffer before acking the initial configure → `UnconfiguredBuffer`; popup grab after being mapped → `InvalidGrab`; popup mapped without parent → error; unmap resets role/cached state but keeps in-flight `pending_configures`. This is *the* canonical hard-to-get-right xdg rule, done declaratively.
- **Positioner**: size and anchor-rect `<1` → `InvalidInput` (`handlers/positioner.rs:38-55`); unknown enum values via `WEnum::Value` are silently ignored rather than trusted.
- **dmabuf import** (`wayland/dmabuf/`): plane idx ≥ `MAX_PLANES` → `PlaneIdx`; duplicate plane idx → `PlaneIdx`; mismatched modifier (v≥5) → `InvalidFormat`; bad dev_t size → `InvalidDevTSize` (`dispatch.rs:253-301`). In `create_dmabuf` (`mod.rs:1052-1182`): already-used → `AlreadyUsed`; unknown/unsupported format → `InvalidFormat`; `w/h<1` → `InvalidDimensions`; **`stride*height + offset` checked-mul overflow → `OutOfBounds`**; **`seek(fd, End)` to get real fd size** then reject `offset>size`, `offset+stride>size`, and (plane 0 only) `end>size` → `OutOfBounds`; after sorting, non-consecutive plane indices → `Incomplete` (`:1106-1182`). Import failure kills the client per protocol note (`dispatch.rs:352-357`).

*C++ copy hint:* the fd-size defense — `lseek(fd, 0, SEEK_END)` and validate `offset`/`offset+stride`/`offset+stride*height` against the actual fd size with **checked** arithmetic — is the concrete anti-malicious-dmabuf check to copy; and enforce the xdg "no buffer before initial ack" rule inside a commit hook, not scattered.

## 3. shm SIGBUS story (the standout)

`src/wayland/shm/pool.rs` installs a process-wide `SIGBUS` handler (once, via `OnceLock`, `SA_SIGINFO|SA_NODEFER`, `:294-313`). Each buffer access (`with_data`/`with_data_mut`, `:102-170`) sets a `thread_local SIGBUS_GUARD` = `(pool_ptr, triggered)` around the closure. If the client truncates the shm file and the compositor faults, `sigbus_handler` (`:323-344`) checks whether the faulting address lies inside the guarded pool (`MemMap::contains`); if so it **`mmap`s an anonymous zero page over the region with `MAP_FIXED`** (`nullify_map`, `:281-291`) so the read completes with zeros, marks `triggered=true`, and the access returns `Err(())` (→ `BufferAccessError`) instead of crashing the whole compositor. A fault outside any known pool re-raises the original SIGBUS (`reraise_sigbus`, `:315-321`). Recursive pool access panics deliberately (`:116,152`). Public API `with_buffer_contents` documents that `NotWritable` is `unreachable!` for the read path (`shm/mod.rs:86,242-291`). `Send`/`Sync` are hand-`unsafe impl`'d, justified by the `RwLock` around the memmap (`:57-60`).

*C++ copy hint:* wrap shm reads with a SIGBUS handler that tests the fault address against the active pool and overwrites it with an anonymous `MAP_FIXED` page, returning an access error — this is the standard way to survive a client that shrinks its pool mid-read, and Hyprland notably lacks it.

## 4. DoS / limits / commit ordering

- **fifo** (`wayland/fifo/mod.rs`): only one fifo object per surface → `AlreadyExists` (`:201-207`); barrier logic lives in a pre-commit hook installed once (`is_managed && is_initial` guard prevents duplicate hooks on surface reuse, `:210-219`); barriers integrate with the `Blocker` transaction system so a set-but-unsignaled barrier just holds the commit, it doesn't spin.
- **Transactions/blockers** serialize dependent surface commits; `transaction.rs:212` panics only on an internal invariant violation (handle-not-last on finalize), i.e. treated as a library bug, not client-reachable.
- Damage/region ops go through pending cached state; scale/offset validated at request time (above).

## 5. Explicit sync / stuck-fence handling

`wayland/drm_syncobj/` — commit-time validation mirrors the spec: `NoBuffer`, `NoAcquirePoint`, `NoReleasePoint`, `ConflictingPoints`, `UnsupportedBuffer`, `SurfaceExists`, `NoSurface`, `InvalidTimeline` (`mod.rs:243-458`). Acquire fences become a `DrmSyncPointBlocker` backed by an **eventfd armed via `syncobj_eventfd`** and registered as a calloop `EventSource` (`sync_point.rs:16-34,116-190,239-269`); the blocker reports `Pending`/`Released` from an `AtomicBool` so a never-signaling (stuck) fence simply keeps the surface's commit pending in the event loop instead of blocking the compositor thread. `wait(timeout_nsec)` (`:144-149`) offers a bounded `syncobj_timeline_wait` for the synchronous path. Release points are signaled when the last `Buffer` ref drops; `merge_into` signals the release point if a superseding commit replaces an un-applied one (`mod.rs:110-117`) so buffers aren't leaked/stuck.

*C++ copy hint:* represent an acquire fence as an eventfd wired into your event loop with a released/pending flag, so a malicious never-signaling fence stalls only that one surface's commit — never the main loop — and always signal the release point when a commit is superseded.

## 6. Panic-vs-protocol-error policy boundary

Clear, consistent split:
- **Client-reachable violations → `resource.post_error(...)`** with the precise interface error enum (hundreds of sites; wire args, sizes, serials, roles, formats).
- **`unreachable!()`** is used only for opcodes/enum arms the wire layer (`wayland-rs`) already validated (`handlers.rs:102,339,410,475,618`, `xdg/mod.rs:570,622`) — i.e. "the protocol parser guarantees this can't happen."
- **`panic!` / `assert!` / `debug_assert!`** reserved for *library-internal invariants and API misuse by the compositor author*, never client input: accessing foreign region data (`compositor/mod.rs:441`), recursive shm access (`pool.rs:116,152`), finalizing a transaction out of order (`transaction.rs:212`), `sigaction` install failure (`pool.rs:307`), region-contains test asserts in unit tests (`mod.rs:749-786`).

*C++ copy hint:* codify the rule — anything a client can trigger posts a typed protocol error and returns; `assert`/`abort` is only for your own broken invariants — and lean on a generated wire-parser so unknown-opcode paths are statically unreachable rather than runtime-checked.

---

## Cross-cutting contrast (for a from-scratch C++ compositor)
- Both centralize resource reaping (smithay via `Drop`/destruction hooks; Hyprland via dual destroy hooks + `erase_if` on `SP<>` containers) and both funnel role assignment through a single guarded choke point.
- Both do identical dmabuf param validation (plane count/gaps, modifier consistency, `offset+stride*height` overflow) — smithay adds the **fd `lseek(SEEK_END)` size check** that Hyprland omits.
- Hyprland uniquely ships **ANR kill dialogs**, a **runtime permission manager**, and an **allowlist global filter** tied to security-context; smithay uniquely ships the **SIGBUS shm jail** and pushes protocol-edge-cases into the **type system + pre-commit hooks**. A robust new compositor wants both halves: smithay's SIGBUS handler and typed error/panic discipline, plus Hyprland's ANR-kill, permission gating, and sandbox-aware global allowlist.


---

## Appendix F: imway gap audit

_Verbatim final message of the `imway robustness gap audit` research agent._

I have gathered comprehensive evidence across all 12 items. Here is the gap inventory.

---

# imway defenses vs. buggy/malicious clients — gap inventory

Evidence paths are absolute; line numbers from the current tree.

## 1. SHM / SIGBUS

- **SIGBUS protection: PRESENT (delegated).** imway never mmaps client shm itself; it uses libwayland's `wl_shm_buffer_begin_access`/`end_access` bracket around every read (`/home/pg/monorepo/imway/wayland.cpp:1599`, and copy sites at `:7125,:7148,:7326,:8434`). That libwayland bracket installs the per-access SIGBUS handler that zero-fills a truncated pool. `headless_sigbus.sh` (`/home/pg/monorepo/imway/dev/tests/headless_sigbus.sh`) just runs `client_sigbus.c` (commits over a truncated pool) then `expect_alive` — it verifies the compositor survives; the actual mechanism is libwayland's `begin_access`, not imway code.
  - **Caveat/gap:** `crash_trace.cpp:102` installs its own `SIGBUS` handler, but only under `IMWAY_FILL_GARBAGE` (poisoned test build), and it re-raises to default — it does not interfere with libwayland's save/restore in production builds.
- **Pool resize validation: ABSENT in imway (delegated to libwayland).** No `wl_shm_pool` implementation exists in the tree; resize/size bounds are libwayland's.
- **stride/offset/size checks in copyShmBuffer: PRESENT.** `copyShmBufferTo` (`wayland.cpp:1572-1634`): format whitelist ARGB8888/XRGB8888 only (`:1578`, rejects others gracefully → `outW=outH=0`); explicit guard `stride < (i64)w*4` against libwayland's width-only stride validation (`:1587-1592`, comment names the exact attack); all index math uses `size_t`/`i64` casts. XRGB alpha is force-set to 0xff (`:1620-1632`).
  - **Gap:** no upper cap on `w`/`h`; `out.grow((size_t)w*h*4)` (`:1609`) trusts libwayland's pool-fit check. An oversized commit is only stopped later at texture creation (see item 6/11).

## 2. dmabuf

- **Plane/offset/stride validation: PRESENT and thorough** in `paramsAdd`/`paramsMakeBuffer` (`wayland.cpp:8793-8939`): plane_idx range (`:8803` `kDmabufMaxPlanes`), double-set (`:8812`), one-modifier-across-planes (`:8821`), nonzero stride (`:8830`), positive dimensions (`:8858`), plane-0 present + all planes present (`:8866,:8872`), **32-bit overflow check** `offset + stride*height > 0xffffffff` (`:8879-8885`), format+modifier support (`:8888`), exact plane count per format (`:8894`), flags must be 0 (`:8904`).
- **fd import test: PRESENT but not a size check.** `dmabufFdsImportable` (`:8762`) round-trips each fd through `drmPrimeFDToHandle`+`GEM_CLOSE` to prove importability; returns `false` → `send_failed`/protocol error. **Gap: there is NO `lseek(SEEK_END)`/`fstat` size verification that the fd is actually ≥ offset+stride*height.** The overflow check is only against 2^32, not against real fd size — a small fd with in-range-but-oversized layout passes params validation and defers to the kernel/Vulkan import.
- **Import failure → error, not crash: PRESENT.** `renderer.cpp importDmabuf` (`:2192-2485`) checks nplanes range (`:2199`), verifies plane fds share dev/ino (`:2247-2251`), and on `vkCreateImage`/memory-import/view failure logs and returns `false` (`:2303,:2421,:2452`) — surface stays untextured, no abort. Multi-plane NV12/P010 handled.
- **Format/modifier table trust: PRESENT.** `formatSupported`/`formatList` gate on the device-advertised table; the fd-import self-test backstops a lying modifier.

## 3. Explicit sync (drm_syncobj)

- **Stuck acquire fence timeout: PRESENT — confirmed 100 ms.** `renderer.cpp:4080-4128`: tries GPU path first (`drmSyncobjTransfer`+`ExportSyncFile`+`waitOnSyncFile` with `WAIT_FOR_SUBMIT`); on failure falls back to a CPU `drmSyncobjTimelineWait` with `deadline = now + 100000000ns` (`:4111-4117`). **After timeout:** logs `"acquire point unavailable, sampling unsynchronized"` and draws the surface anyway once (does not re-stall every frame, does not crash) — a graceful degrade, not a block.
- **No-acquire-point / conflicting-points / unsupported-buffer: PRESENT** as protocol errors (`wayland.cpp:1876,:1888,:1894`).
- **Release point signaling on client death: PRESENT.** `DmabufUse::~DmabufUse` (`wayland.cpp:10047-10068`) calls `drmSyncobjTimelineSignal(rel)` whenever a held buffer with a release timeline is torn down; `releaseHeldDmabuf` (`:1403`) runs on `surfaceResourceDestroyed` (`:2492`), so a peer waiting on the release point is unblocked even if the owning client dies. `releaseCachedDmabuf` signals it for the synced-subsurface cache path too (`:1371`).

## 4. xdg-shell

- **ack_configure serial validation: PRESENT.** `xdgSurfaceAckConfigure` (`:3234-3263`) searches `configureSerials`, posts `XDG_SURFACE_ERROR_INVALID_SERIAL` for unknown serials (`:3247`), and prunes acked+older entries.
- **Role conflict errors: PRESENT** (subcompositor `:2825`, xdg role reuse tests `client_err_xdg_already_constructed`, `client_err_subsurface_role`, `client_err_pointer_cursor_role`, `client_err_dnd_icon_role`).
- **Window-geometry validation: PRESENT** (`client_err_xdg_geometry` test; geometry handling in toplevel path).
- **Positioner validation: PRESENT** — dedicated errors for anchor/gravity/size/constraints/parent-size (`client_err_positioner_*` × 5; constrain math at `wayland.cpp:9465-9535` all in `i64` with `clampPosition`).
- **Popup grab serial: PRESENT with epoch check.** `popupGrab` (`:3462-3478`) posts `XDG_POPUP_ERROR_INVALID_GRAB` unless `pointerGrabSerial == serial && buttonsDown > 0`, and `keyGrabGeneration == focusGeneration` (`:3481`) — a stale grab serial from a prior focus epoch is rejected.
- **Popup parent death: covered** (`client_err_popup_chain_teardown`, `client_err_popup_topmost`).

## 5. Unresponsive clients

- **ping/pong: PRESENT for detection, NO kill.** A 5 s repeating timer (`pingTimerCb` `:1207-1216`, armed `:11350`) sends `xdg_wm_base_send_ping` to every `wl_wm_base`; if the prior ping was un-acked it only logs `"client is not answering ping"` (`:1212`) and pings again. **Gap: no ANR/kill machinery, no focus/rendering penalty for a frozen client.** `wmBasePong` (`:1181`) just marks acked.
- **Frame-callback starvation: N/A.** A frozen client simply stops committing; its old buffer keeps being composited. No watchdog kills or reaps it.

## 6. Flooding / DoS

- **MIME offers per source: CAPPED at 64** (`:4011`, `:5617`) — excess silently dropped; per-mime string bounded by `sizeof(Mime::s)`.
- **Activation grants queue: CAPPED at 64** FIFO-evicted (`:8636-8644`).
- **Input serials: CAPPED at 64** ring (`:10562-10573`).
- **Icons: CAPPED** — `kIconMaxSize=1024` (`:713`), `kIconMaxBuffers=64` (`:714`, enforced `:8412`).
- **Subsurface cycles: PREVENTED** (bounded ancestor walk, `:2831-2848`).
- **Damage rect coordinates: CLAMPED** to INT32 with overflow guard (`:1486-1496`).
- **VkTexturePool: growable, NO hard cap but fails gracefully.** `tex_pool.cpp`: doubling chunks, chunk *size* clamped at `1024<<16`, but chunk *count* is unbounded; on device OOM `grow()` returns null → `alloc` returns null → surface left untextured (`renderer.cpp:1705-1713`), no crash.
- **GAPS:**
  - **Frame callbacks per surface: UNBOUNDED** — `s.frameCbs.pushBack(cb)` (`:1788`) with no cap; `client_reg_frame_callback_flood.c` floods 4096 and only tests destroy-before-fire cleanup, not a limit. Callbacks are drained each frame, so it's a transient memory spike, not permanent.
  - **No cap on surface / toplevel / wl_output-bind / region / subsurface / activation-token-object / pointer-object counts.** `client_reg_pointer_objects_flood`, `client_reg_idle_flood` verify *survival*, relying on libwayland object handling + arena allocator — there is no imway-imposed ceiling.
  - **Buffer dimension cap: ABSENT as an explicit check** — oversized dims are only rejected reactively when `vkCreateImage` throws (caught in `uploadSurface`, item 11).
  - **Damage rect *count*:** merged via `unionRect` (single bbox), so unbounded damage submissions coalesce — effectively bounded.
  - **Socket backpressure:** relies on libwayland `wl_display` flush; no explicit imway flush-error handling seen.

## 7. Serial-validation coverage

- **focusGeneration epoch scheme: PRESENT.** `focusGeneration` starts at 1 (`:871`), incremented on every focus change (`:11066,:11165,:11190,:11263`). Each remembered serial stores its epoch (`InputSerial.focusGeneration`, `:863`); `validSerial` requires `client` match **and** `entry.focusGeneration == focusGeneration` (`:10576-10587`), so a serial retained across a focus loss is rejected.
- **Requests checking serials:** toplevel move/resize (`validToplevelGrab` `:2943`), popup grab (`:3462`), selection/primary-selection (`validSelectionSerial` `:10651`), DnD `startDrag` (`:912`), xdg-activation `set_serial` (`:8633`), pointer `set_cursor` (`validPointerEnter` `:10603`), cursor-shape (`validCursorShapeEnter` `:10630`). Broad and consistent.

## 8. DnD / selection

- **Source death mid-drag: covered extensively.** `client_reg_dnd_kill_source_{enter,motion,drop,finish}` and `_kill_target_{enter,motion,drop,finish}` — an 8-cell kill matrix — plus `dnd_kill_phases.inc`. Source/offer/target/icon destroy each have regression tests (`client_reg_dnd_destroy_{icon,offer,target}`, `_null_source`, `_source_leave`). Code path: `dragSource` (`:852`) + resource destroy listeners clear seat drag/target/grab-origin/scene dragIcon (`surfaceResourceDestroyed` comment `:2432`).
- **Protocol-error coverage: PRESENT** — 13 `client_err_dnd_*` (used-source, action-mask, early/double finish, receive/accept-after-finish, dead-source-offer, drag-vs-selection role mixing, etc.).
- **fd/CLOEXEC hygiene: PARTIAL evidence.** Socket accept uses `accept4(..., SOCK_CLOEXEC|SOCK_NONBLOCK)` (`:6295`); format table uses `MFD_CLOEXEC` (`:12558`). DnD/selection pipe fds are passed through libwayland's `wl_data_source.send`/`receive`; no imway-side `dup`/`recvmsg` seen — fd receipt is libwayland's (which sets CLOEXEC).
- **mime string limits: PRESENT** — capped count (64) and bounded copy into `Mime::s` (`:4011,:5617`).

## 9. Wire robustness

- **Malformed-byte tests: PRESENT (delegated to libwayland).** `client_err_wire_{short_header,opcode,object,unaligned,duplicate_id,missing_fd}` send hand-crafted bytes via `wire_send` (bypassing libwayland marshalling) and assert `wire_wait_closed()` — proving the wire parser disconnects the offending client without harming the compositor. This is libwayland's parser, not imway code.
- **fd exhaustion: PARTIAL** — `client_reg_wire_{extra_fd,fd_cleanup}` verify leaked/extra fds are reclaimed; no explicit imway fd-limit handling beyond libwayland.
- **Disconnect mid-request: PRESENT** — `client_reg_wire_{midrequest_disconnect,flush_disconnect,truncated}` verify clean teardown.

## 10. Overflow arithmetic

- **Damage:** buffer-coord scaling done in `i64` with explicit `[minI32,maxI32]` clamp (`:1486-1496`).
- **dmabuf plane layout:** `(u64)offset + (u64)stride*(u64)height > 0xffffffff` (`:8879`).
- **Popup positioner:** all anchor/gravity/flip/slide math in `i64`, `clampPosition(i64)` saturates to INT32 (`:3409,:9465-9535`).
- **shm stride:** `stride < (i64)w*4` (`:1587`, `:8405`).
- **Presentation/idle timestamps:** saturating `(u64)hi<<32|lo` with "never" saturation (`:5507`).
- **Region rects:** intersect/union in `i64` (`:2541,:2563,:9751`).
- **screencopy offset:** `(u64)offset + length > st_size` (`:12162`).
- Overall this is a **strong, consistently-applied** area.

## 11. Abort policy / client-reachable aborts

- **`STD_VERIFY` THROWS, does not abort.** `third_party/libstd/std/dbg/verify.h` → `raiseVerify` `throw`s `VerifyError` (`throw.cpp:82`). `VK_CHECK` = `STD_VERIFY(...==VK_SUCCESS)` (`device_vk.h:10`).
- **Top-level catch: PRESENT but coarse.** `main_composer.cpp:200-354` wraps the *entire* `wayland->run()`/`ev_run` in one `try/catch(...)` that logs `"fatal"` and exits(1). So an uncaught throw from any client-request callback would unwind through libwayland/libev C frames and **terminate the whole session** (graceful exit, but total DoS).
- **The 6 `STD_VERIFY` in wayland.cpp are all startup/internal, NOT client-reachable:** getrandom (`:5035`), keyboard init (`:9551`), display (`:11306`), `wl_global_create` (`:12473`), fbTable fd/write (`:12559,:12568`).
- **Client-sized Vulkan allocation is defended:** `renderer.cpp uploadSurface` wraps `createImage`/`createHostBuffer` in `try/catch(...)` (`:1685-1694`) converting a `VK_CHECK` throw into a logged skip — so an oversized/hostile shm buffer degrades gracefully rather than aborting.
- **`imgui` VkCheck DOES hard-abort** (see item 12) — the one genuine abort path, reachable if client content forces an imgui Vulkan error.
- **Net:** no client-reachable `abort()`/`STD_VERIFY` found in the request handlers except through imgui (item 12); the design deliberately routes client faults to `wl_resource_post_error`. Residual risk = any *unwrapped* `VK_CHECK` on a client-influenced path throwing out to the coarse top-level catch (session exit).

## 12. GPU robustness

- **`VK_ERROR_DEVICE_LOST`: NO specific handling / NO reset recovery.** No `DEVICE_LOST` string anywhere in `renderer.cpp`. There is no `vkDeviceWaitIdle`+recreate path.
- **imgui `CheckVkResultFn`: HARD ABORT.** `imguiVkCheck` (`renderer.cpp` near `:1542`) calls `abort()` on any `err < 0` — including `DEVICE_LOST` — after logging `"fatal: imgui vulkan call failed"`. This is the sharpest failure edge.
- **Main queue submit failure: graceful exit, not recovery.** `vkQueueSubmit` non-success → `ev_break(EVBREAK_ALL)` + return false (`:4350-4356`); readback submit → return false (`:4467`).
- **Fence waits use `UINT64_MAX` (infinite).** `vkWaitForFences(..., UINT64_MAX)` at `:927,:2842,:4473` — a hung/lost GPU can **hang the compositor indefinitely** at these points (no timeout).
- **Net:** device-lost / GPU-reset resilience is effectively **ABSENT** — outcomes are abort (imgui), clean-exit (submit), or infinite hang (fence wait). This is the weakest defensive area.

---

### Highest-value gaps (all client-triggerable)
1. **GPU device-lost = abort/hang** (item 12): imgui `abort()` + `UINT64_MAX` fence waits, no recovery.
2. **dmabuf fd has no real size check** (item 2): only 2^32 overflow guard, no `lseek`/`fstat` vs `offset+stride*height`.
3. **No ANR kill for frozen clients** (item 5): ping only logs.
4. **No absolute caps on surface/toplevel/output-bind/frame-callback/object counts** (item 6): survival relies on libwayland + arena, no ceiling.
5. **Coarse top-level catch** (item 11): any unwrapped throw from a client callback exits the whole session.

Strong areas: overflow arithmetic (item 10), serial+focusGeneration epochs (item 7), dmabuf params validation and DnD teardown matrices (items 2/8), and SIGBUS+wire robustness (delegated correctly to libwayland, items 1/9).


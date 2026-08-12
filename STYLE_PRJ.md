# Project style settings

Per-project settings that the shared [STYLE.md](STYLE.md) delegates here.

- **Macro prefix.** None reserved. Wayland protocol macros (`ZWP_`, `WP_`,
  `XDG_`, ...) keep their external spelling.
- **Namespace.** Imway is a program: no project namespace. The vendored
  `plt` platform library keeps its `plt` namespace.
- **Formatter.** `./style.py` formats every tracked C++ source.

## Deviations

- Do not translate allocation failure to `wl_client_post_no_memory`; if the
  process cannot allocate its ordinary memory, let the exception reach the
  top-level handler.
- Wayland resource creation and GPU allocation/import with a real backend
  fallback are meaningful local recovery paths. `VK_CHECK` may be caught at
  a narrow GPU fallback boundary; without such a fallback its failure
  reaches the top-level handler.

// xdg_wm_base / xdg_surface / xdg_toplevel / xdg_popup / xdg_positioner.
#pragma once

struct Popup;
struct Server;
struct Surface;
struct Toplevel;

void xdgShellCreateGlobal(Server&);

// реакция xdg-роли на commit поверхности (map-логика, configure dance)
void xdgHandleCommit(Surface&);
// послать клиенту configure с новым размером (ресайз ImGui-окном)
void xdgToplevelConfigureSize(Toplevel&, int w, int h);
// закрыть попап (popup_done + unmap); клиент затем уничтожит ресурс
void xdgPopupDismiss(Popup&);

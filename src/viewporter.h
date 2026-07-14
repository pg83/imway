// wp_viewporter: кроп и масштабирование поверхностей.
#pragma once

struct Server;
struct Surface;

void viewporterCreateGlobal(Server&);

// применить pending-состояние вьюпорта на commit
void viewportApplyPending(Surface&);
// при уничтожении поверхности вьюпорт становится инертным
void viewportSurfaceGone(Surface&);

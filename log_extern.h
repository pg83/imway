#pragma once

#include <stdarg.h>

#include <std/str/view.h>

struct Composer;
struct Log;

// Route the messages of the external libraries into Composer::log, so
// nothing the compositor links talks past the ring straight to stderr.
// Installs the process-global hooks (libwayland server/client, libseat,
// lcms2); the per-context ones live with their owners: libinput in
// input.cpp, xkbcommon in keyboard.cpp, the Vulkan debug messenger in
// device_vk. Call right after the Log exists, before any subsystem.
void installExternLogHandlers(Composer& c);

// reformat a C library's printf-style message into one tagged log line
// ("tag: message", the trailing newline stripped)
void externVLog(Log& log, stl::StringView tag, const char* fmt, va_list args);

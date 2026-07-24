#include "log_extern.h"

#include "log.h"
#include "util.h"
#include "composer.h"

#include <std/ios/manip.h>

#include <lcms2.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include <libseat.h>
}

#include <wayland-client-core.h>
#include <wayland-server-core.h>

using namespace stl;

namespace {
    // the libwayland and libseat handlers carry no user data: the one log of
    // this process is reachable only through a static
    Log* externLogSink = nullptr;

    void wlServerLog(const char* fmt, va_list args) {
        externVLog(*externLogSink, "wayland"_sv, fmt, args);
    }

    void wlClientLog(const char* fmt, va_list args) {
        externVLog(*externLogSink, "wayland-client"_sv, fmt, args);
    }

    void seatLog(enum libseat_log_level, const char* fmt, va_list args) {
        externVLog(*externLogSink, "seat"_sv, fmt, args);
    }

    void lcmsLog(cmsContext, cmsUInt32Number code, const char* text) {
        *externLogSink << "lcms: error "_sv << (u64)code << ": "_sv << StringView(text) << endL;
    }
}

void externVLog(Log& log, StringView tag, const char* fmt, va_list args) {
    // reformatting a foreign va_list is the one legitimate printf here; a
    // message past the buffer is truncated, which a log line can afford
    char buf[2048];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);

    if (n < 0) {
        return;
    }

    size_t len = (size_t)n < sizeof(buf) - 1 ? (size_t)n : sizeof(buf) - 1;

    while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        len--;
    }

    log << tag << ": "_sv << StringView((const u8*)buf, len) << endL;
}

void installExternLogHandlers(Composer& c) {
    externLogSink = c.log;

    wl_log_set_handler_server(wlServerLog);
    wl_log_set_handler_client(wlClientLog);
    libseat_set_log_handler(seatLog);
    libseat_set_log_level(LIBSEAT_LOG_LEVEL_INFO);
    cmsSetLogErrorHandler(lcmsLog);
}

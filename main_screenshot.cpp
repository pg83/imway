#include "main_screenshot.h"
#include "util.h"

#include <fcntl.h>
#include <unistd.h>

#include <std/ios/sys.h>
#include <std/sys/types.h>

using namespace stl;

// stub — the wayland+vulkan imgui cropper lands in the next step; for now it
// opens the handed-over memfd, validates the self-describing header and
// reports the image, proving the /proc/self/fd handoff
int mainScreenshot(StringView path) {
    Buffer p(path);
    int fd = open(p.cStr(), O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        sysE << "imway screenshot: cannot open "_sv << path << endL;

        return 1;
    }

    struct {
        u32 magic;
        u32 w;
        u32 h;
    } hdr = {};

    ssize_t n = read(fd, &hdr, sizeof(hdr));

    close(fd);

    if (n != (ssize_t)sizeof(hdr) || hdr.magic != 0x31574d49u) {
        sysE << "imway screenshot: bad image header"_sv << endL;

        return 1;
    }

    sysO << "imway screenshot: got "_sv << (i64)hdr.w << "x"_sv << (i64)hdr.h << " image"_sv << endL;

    return 0;
}

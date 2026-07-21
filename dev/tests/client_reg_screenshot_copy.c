// End-to-end receiver for the screenshot cropper's Copy action. It validates
// both image/jxl and the compatibility image/png payload, then replaces the
// selection so the hidden cropper is cancelled and can exit.

#include "wl_util.h"

#include <jxl/color_encoding.h>
#include <jxl/decode.h>
#include <math.h>
#include <png.h>

static struct wl_toplevel_ctx top;
static struct wl_data_device* device;
static struct wl_data_offer* selection;
static int image_png;
static int image_jxl;

static void offer_mime(void* data, struct wl_data_offer* offer, const char* mime) {
    (void)data; (void)offer;
    if (!strcmp(mime, "image/png")) image_png = 1;
    if (!strcmp(mime, "image/jxl")) image_jxl = 1;
}

static void offer_source_actions(void* data, struct wl_data_offer* offer, uint32_t actions) {
    (void)data; (void)offer; (void)actions;
}

static void offer_action(void* data, struct wl_data_offer* offer, uint32_t action) {
    (void)data; (void)offer; (void)action;
}

static const struct wl_data_offer_listener offer_listener = {
    offer_mime, offer_source_actions, offer_action,
};

static void device_offer(void* data, struct wl_data_device* dev, struct wl_data_offer* offer) {
    (void)data; (void)dev;
    wl_data_offer_add_listener(offer, &offer_listener, NULL);
}

static void device_enter(void* data, struct wl_data_device* dev, uint32_t serial,
                         struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
                         struct wl_data_offer* offer) {
    (void)data; (void)dev; (void)serial; (void)surface; (void)x; (void)y; (void)offer;
}

static void device_leave(void* data, struct wl_data_device* dev) {
    (void)data; (void)dev;
}

static void device_motion(void* data, struct wl_data_device* dev, uint32_t time,
                          wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)dev; (void)time; (void)x; (void)y;
}

static void device_drop(void* data, struct wl_data_device* dev) {
    (void)data; (void)dev;
}

static void device_selection(void* data, struct wl_data_device* dev,
                             struct wl_data_offer* offer) {
    (void)data; (void)dev;
    selection = offer;
}

static const struct wl_data_device_listener device_listener = {
    device_offer, device_enter, device_leave, device_motion, device_drop,
    device_selection,
};

static void source_target(void* data, struct wl_data_source* source, const char* mime) {
    (void)data; (void)source; (void)mime;
}

static void source_send(void* data, struct wl_data_source* source, const char* mime, int32_t fd) {
    (void)data; (void)source; (void)mime;
    close(fd);
}

static void source_cancelled(void* data, struct wl_data_source* source) {
    (void)data; (void)source;
}

static void source_drop(void* data, struct wl_data_source* source) {
    (void)data; (void)source;
}

static void source_finished(void* data, struct wl_data_source* source) {
    (void)data; (void)source;
}

static void source_action(void* data, struct wl_data_source* source, uint32_t action) {
    (void)data; (void)source; (void)action;
}

static const struct wl_data_source_listener source_listener = {
    source_target, source_send, source_cancelled, source_drop, source_finished,
    source_action,
};

static size_t receive_prefix(const char* mime, unsigned char* data, size_t len) {
    int fds[2];

    if (pipe(fds) < 0) {
        perror("pipe");
        return 0;
    }

    wl_data_offer_receive(selection, mime, fds[1]);
    close(fds[1]);
    wl_display_flush(wl_dpy);

    size_t used = 0;

    while (used < len) {
        wl_display_roundtrip(wl_dpy);
        ssize_t n = read(fds[0], data + used, len - used);

        if (n <= 0) break;
        used += (size_t)n;
    }

    close(fds[0]);
    return used;
}

static size_t receive_all(const char* mime, unsigned char** result) {
    int fds[2];

    if (pipe(fds) < 0) {
        perror("pipe");
        return 0;
    }

    wl_data_offer_receive(selection, mime, fds[1]);
    close(fds[1]);
    wl_display_flush(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    size_t used = 0;
    size_t size = 64 * 1024;
    unsigned char* data = (unsigned char*)malloc(size);

    if (!data) {
        close(fds[0]);
        return 0;
    }

    for (;;) {
        if (used == size) {
            size_t next_size = size * 2;
            unsigned char* next = (unsigned char*)realloc(data, next_size);

            if (!next) {
                free(data);
                close(fds[0]);
                return 0;
            }

            data = next;
            size = next_size;
        }

        ssize_t n = read(fds[0], data + used, size - used);

        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        used += (size_t)n;
    }

    close(fds[0]);
    *result = data;
    return used;
}

static int validate_sdr_png(const unsigned char* data, size_t size) {
    int srgb = 0;

    for (size_t at = 8; at + 12 <= size;) {
        uint32_t length = ((uint32_t)data[at] << 24) |
                          ((uint32_t)data[at + 1] << 16) |
                          ((uint32_t)data[at + 2] << 8) | data[at + 3];

        if ((size_t)length > size - at - 12) return 0;
        if (!memcmp(data + at + 4, "sRGB", 4)) srgb = 1;
        at += 12 + length;
    }

    png_image image = {0};
    image.version = PNG_IMAGE_VERSION;

    if (!srgb || !png_image_begin_read_from_memory(&image, data, size)) {
        return 0;
    }

    image.format = PNG_FORMAT_RGB;
    size_t bytes = PNG_IMAGE_SIZE(image);
    unsigned char* pixels = (unsigned char*)malloc(bytes);

    if (!pixels || !png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
        free(pixels);
        png_image_free(&image);
        return 0;
    }

    size_t green = 0;
    int best_score = -1000;
    unsigned char best[3] = {0};

    for (size_t at = 0; at + 2 < bytes; at += 3) {
        int score = (int)pixels[at + 1] - pixels[at] - pixels[at + 2];

        if (score > best_score) {
            best_score = score;
            memcpy(best, pixels + at, 3);
        }

        // The headless capture is an 8-bit PQ compatibility path, so decoding
        // its quantized dark channels can leave a small SDR residue. The green
        // channel must nevertheless recover to the original saturated SDR
        // surface rather than remain near its PQ code value.
        if (pixels[at] <= 50 && pixels[at + 1] >= 240 &&
            pixels[at + 2] <= 50) {
            green++;
        }
    }

    free(pixels);
    png_image_free(&image);

    if (green < 1000) {
        fprintf(stderr, "PNG diagnostics: srgb=%d size=%ux%u green=%zu best=%u,%u,%u\n",
                srgb, image.width, image.height, green,
                best[0], best[1], best[2]);
    }

    return green >= 1000;
}

static int validate_hdr_jxl(const unsigned char* data, size_t size) {
    JxlDecoder* dec = JxlDecoderCreate(NULL);

    if (!dec) return 0;

    int basic_ok = 0;
    int color_ok = 0;
    int pixels_ok = 0;
    JxlBasicInfo basic = {0};
    uint16_t* pixels = NULL;
    size_t pixels_size = 0;
    JxlPixelFormat format = {3, JXL_TYPE_UINT16, JXL_NATIVE_ENDIAN, 0};

    if (JxlDecoderSubscribeEvents(
            dec, JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING |
                 JXL_DEC_FULL_IMAGE) !=
            JXL_DEC_SUCCESS ||
        JxlDecoderSetInput(dec, data, size) != JXL_DEC_SUCCESS) {
        JxlDecoderDestroy(dec);
        return 0;
    }

    JxlDecoderCloseInput(dec);

    for (;;) {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec);

        if (status == JXL_DEC_BASIC_INFO) {
            if (JxlDecoderGetBasicInfo(dec, &basic) == JXL_DEC_SUCCESS) {
                basic_ok = basic.bits_per_sample == 16 &&
                           fabsf(basic.intensity_target - 600.0f) < 0.01f &&
                           fabsf(basic.min_nits - 0.01f) < 0.0001f;
            }
        } else if (status == JXL_DEC_COLOR_ENCODING) {
            JxlColorEncoding color;

            if (JxlDecoderGetColorAsEncodedProfile(
                    dec, JXL_COLOR_PROFILE_TARGET_ORIGINAL, &color) ==
                JXL_DEC_SUCCESS) {
                color_ok = color.color_space == JXL_COLOR_SPACE_RGB &&
                           color.white_point == JXL_WHITE_POINT_D65 &&
                           color.primaries == JXL_PRIMARIES_2100 &&
                           color.transfer_function == JXL_TRANSFER_FUNCTION_PQ;
            }
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            if (JxlDecoderImageOutBufferSize(dec, &format, &pixels_size) !=
                    JXL_DEC_SUCCESS ||
                !(pixels = (uint16_t*)malloc(pixels_size)) ||
                JxlDecoderSetImageOutBuffer(dec, &format, pixels, pixels_size) !=
                    JXL_DEC_SUCCESS) {
                break;
            }
        } else if (status == JXL_DEC_FULL_IMAGE) {
            size_t count = pixels_size / (3 * sizeof(*pixels));
            size_t green = 0;

            for (size_t i = 0; i < count; i++) {
                uint16_t r = pixels[i * 3 + 0];
                uint16_t g = pixels[i * 3 + 1];
                uint16_t b = pixels[i * 3 + 2];

                if (r >= 29000 && r <= 32000 &&
                    g >= 36000 && g <= 39000 &&
                    b >= 21500 && b <= 24500) {
                    green++;
                }
            }

            pixels_ok = green >= 1000;
        } else if (status == JXL_DEC_SUCCESS) {
            break;
        } else if (status == JXL_DEC_ERROR ||
                   status == JXL_DEC_NEED_MORE_INPUT) {
            break;
        }
    }

    free(pixels);
    JxlDecoderDestroy(dec);
    return basic_ok && color_ok && pixels_ok;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);

    if (wl_boot()) return 1;
    if (!wl_ddm || !wl_seat_g || !wl_kbd) {
        fprintf(stderr, "missing clipboard globals\n");
        return 1;
    }

    wl_make_toplevel(&top, "screenshot-copy-receiver", 320, 180, 0xFF00FF00);
    device = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(device, &device_listener, NULL);
    printf("copy receiver ready\n");

    for (int i = 0; i < 1000 && (!selection || !image_png || !image_jxl); i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(10000);
    }

    if (!selection || !image_png || !image_jxl) {
        fprintf(stderr, "screenshot selection lacks image/jxl or image/png\n");
        return 1;
    }

    unsigned char signature[8] = {};
    size_t used = receive_prefix("image/png", signature, sizeof(signature));

    static const unsigned char png_signature[8] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
    };

    if (used != sizeof(signature) || memcmp(signature, png_signature, sizeof(signature))) {
        fprintf(stderr, "screenshot clipboard payload is not PNG\n");
        return 1;
    }

    unsigned char* png = NULL;
    size_t png_size = receive_all("image/png", &png);

    if (!png_size || !validate_sdr_png(png, png_size)) {
        fprintf(stderr, "PNG screenshot is not tagged and tone-mapped SDR sRGB\n");
        free(png);
        return 1;
    }

    free(png);

    memset(signature, 0, sizeof(signature));
    used = receive_prefix("image/jxl", signature, sizeof(signature));

    static const unsigned char jxl_container_signature[8] = {
        0x00, 0x00, 0x00, 0x0c, 'J', 'X', 'L', ' ',
    };
    int jxl_codestream = used >= 2 && signature[0] == 0xff && signature[1] == 0x0a;
    int jxl_container = used == sizeof(signature) &&
                        !memcmp(signature, jxl_container_signature,
                                sizeof(signature));

    if (!jxl_codestream && !jxl_container) {
        fprintf(stderr, "screenshot clipboard payload is not JPEG XL\n");
        return 1;
    }

    unsigned char* jxl = NULL;
    size_t jxl_size = receive_all("image/jxl", &jxl);

    if (!jxl_size || !validate_hdr_jxl(jxl, jxl_size)) {
        fprintf(stderr, "JPEG XL screenshot metadata or decoded pixels are wrong\n");
        free(jxl);
        return 1;
    }

    free(jxl);

    for (int i = 0; i < 300 && wlk_focus != top.surface; i++) {
        wl_display_roundtrip(wl_dpy);
        usleep(10000);
    }

    if (wlk_focus != top.surface || !wlk_enter_serial) {
        fprintf(stderr, "receiver did not regain keyboard focus\n");
        return 1;
    }

    struct wl_data_source* replacement = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(replacement, &source_listener, NULL);
    wl_data_source_offer(replacement, "text/plain");
    wl_data_device_set_selection(device, replacement, wlk_enter_serial);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    printf("screenshot jxl and png ok\n");
    return 0;
}

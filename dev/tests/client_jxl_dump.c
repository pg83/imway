// jxl -> ppm dump, a debugging aid for screenshots saved by the viewer:
//   client_jxl_dump shot.jxl out.ppm
// HDR (PQ) content dumps with its transfer intact — good enough to inspect
// structure, not colorimetry.
#include <stdio.h>
#include <stdlib.h>

#include <jxl/decode.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s in.jxl out.ppm\n", argv[0]);
        return 2;
    }

    FILE* in = fopen(argv[1], "rb");

    if (!in) {
        perror(argv[1]);
        return 1;
    }

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);

    unsigned char* data = malloc((size_t)size);

    if (!data || fread(data, 1, (size_t)size, in) != (size_t)size) {
        fprintf(stderr, "read failed\n");
        return 1;
    }

    fclose(in);

    JxlDecoder* dec = JxlDecoderCreate(NULL);
    JxlBasicInfo basic = {0};
    unsigned char* pixels = NULL;
    size_t pixels_size = 0;
    JxlPixelFormat format = {3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};

    if (!dec ||
        JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS ||
        JxlDecoderSetInput(dec, data, (size_t)size) != JXL_DEC_SUCCESS) {
        fprintf(stderr, "decoder setup failed\n");
        return 1;
    }

    JxlDecoderCloseInput(dec);

    for (;;) {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec);

        if (status == JXL_DEC_BASIC_INFO) {
            if (JxlDecoderGetBasicInfo(dec, &basic) != JXL_DEC_SUCCESS) {
                fprintf(stderr, "no basic info\n");
                return 1;
            }
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            if (JxlDecoderImageOutBufferSize(dec, &format, &pixels_size) != JXL_DEC_SUCCESS ||
                !(pixels = malloc(pixels_size)) ||
                JxlDecoderSetImageOutBuffer(dec, &format, pixels, pixels_size) != JXL_DEC_SUCCESS) {
                fprintf(stderr, "out buffer failed\n");
                return 1;
            }
        } else if (status == JXL_DEC_FULL_IMAGE || status == JXL_DEC_SUCCESS) {
            break;
        } else if (status == JXL_DEC_ERROR) {
            fprintf(stderr, "decode error\n");
            return 1;
        }
    }

    if (!pixels) {
        fprintf(stderr, "no image\n");
        return 1;
    }

    FILE* out = fopen(argv[2], "wb");

    if (!out) {
        perror(argv[2]);
        return 1;
    }

    fprintf(out, "P6\n%u %u\n255\n", basic.xsize, basic.ysize);
    fwrite(pixels, 1, (size_t)basic.xsize * basic.ysize * 3, out);
    fclose(out);
    printf("%ux%u\n", basic.xsize, basic.ysize);

    return 0;
}

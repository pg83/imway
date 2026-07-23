#include "glow.h"

#include <math.h>

namespace {
    // sprite geometry, in bake pixels: the flat rounded core is the middle
    // half, the gaussian skirt takes the rest
    constexpr int kGlowPx = 64;
    constexpr float kGlowCore = 16.f;   // core half-side
    constexpr float kGlowRadius = 6.f;  // core corner radius
    constexpr float kGlowSigma = 5.f;   // skirt falloff

    ImFontAtlas* glowAtlas = nullptr;
    int glowRect = -1; // ImFontAtlasRectId
}

void bakeGlow(ImFontAtlas* atlas) {
    ImFontAtlasRect r;
    ImFontAtlasRectId id = atlas->AddCustomRect(kGlowPx, kGlowPx, &r);

    if (id == ImFontAtlasRectId_Invalid) {
        return;
    }

    ImTextureData* tex = atlas->TexData;

    for (int y = 0; y < kGlowPx; y++) {
        for (int x = 0; x < kGlowPx; x++) {
            float half = kGlowCore - kGlowRadius;
            float qx = fabsf((float)x + 0.5f - kGlowPx / 2.f) - half;
            float qy = fabsf((float)y + 0.5f - kGlowPx / 2.f) - half;
            float ox = qx > 0.f ? qx : 0.f;
            float oy = qy > 0.f ? qy : 0.f;
            float d = sqrtf(ox * ox + oy * oy) - kGlowRadius;
            float a = d <= 0.f ? 1.f : expf(-d * d / (2.f * kGlowSigma * kGlowSigma));

            // steepen the skirt perceptually: the HDR path blends the ui in
            // linear light, where a shallow alpha ramp of a warm tint over a
            // dark background washes into a pale fog
            a = powf(a, 2.2f);
            unsigned char av = (unsigned char)(a * 255.f + 0.5f);
            unsigned char* p = (unsigned char*)tex->GetPixelsAt(r.x + x, r.y + y);

            if (tex->Format == ImTextureFormat_Alpha8) {
                p[0] = av;
            } else {
                p[0] = p[1] = p[2] = 255;
                p[3] = av;
            }
        }
    }

    glowAtlas = atlas;
    glowRect = id;
}

void drawGlow(ImDrawList* dl, ImVec2 min, ImVec2 max, float reach, ImU32 color) {
    if (glowRect < 0) {
        return;
    }

    // uv is only valid for the current texture: re-read every draw
    ImFontAtlasRect r;

    if (!glowAtlas->GetCustomRect(glowRect, &r)) {
        return;
    }

    ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    float coreHalf = (max.x - min.x) * 0.5f + reach;
    float half = coreHalf * (kGlowPx / 2.f) / kGlowCore;

    dl->PushClipRectFullScreen();
    dl->AddImage(glowAtlas->TexRef, ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), r.uv0, r.uv1, color);
    dl->PopClipRect();
}

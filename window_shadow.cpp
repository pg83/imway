#include "window_shadow.h"

#include <math.h>

namespace {
    // sprite geometry, in bake pixels: a solid rounded core of kCore
    // surrounded by kMargin of gaussian falloff
    constexpr int kMargin = 20;
    constexpr int kCore = 12;
    constexpr int kSprite = kMargin * 2 + kCore;
    constexpr float kRadius = 5.f;  // core corner radius
    constexpr float kSigma = kMargin / 2.6f;
    constexpr int kAlpha = 105; // shadow strength

    // signed distance to the core rounded box, positive outside
    float coreDist(float px, float py) {
        float half = kCore / 2.f - kRadius;
        float cx = kSprite / 2.f, cy = kSprite / 2.f;
        float qx = fabsf(px - cx) - half;
        float qy = fabsf(py - cy) - half;
        float ox = qx > 0.f ? qx : 0.f;
        float oy = qy > 0.f ? qy : 0.f;

        return sqrtf(ox * ox + oy * oy) - kRadius;
    }
}

void bakeWindowShadow(ImFontAtlas* atlas, ShadowSprite& s) {
    ImFontAtlasRect r;
    ImFontAtlasRectId id = atlas->AddCustomRect(kSprite, kSprite, &r);

    if (id == ImFontAtlasRectId_Invalid) {
        return;
    }

    ImTextureData* tex = atlas->TexData;

    for (int y = 0; y < kSprite; y++) {
        for (int x = 0; x < kSprite; x++) {
            float d = coreDist((float)x + 0.5f, (float)y + 0.5f);
            float a = d <= 0.f ? 1.f : expf(-d * d / (2.f * kSigma * kSigma));
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

    s.atlas = atlas;
    s.rectId = id;
}

void drawWindowShadow(ImDrawList* dl, ImVec2 pos, ImVec2 size, float rounding, ImGuiWindowFlags flags, void* user) {
    (void)rounding;

    auto& s = *(ShadowSprite*)user;

    if (s.rectId < 0 || (flags & ImGuiWindowFlags_NoBackground)) {
        return;
    }

    // uv is only valid for the current texture: re-read every draw
    ImFontAtlasRect r;

    if (!s.atlas->GetCustomRect(s.rectId, &r)) {
        return;
    }

    float m = kMargin * s.scale;         // blur reach on screen
    float in = kCore / 2.f * s.scale;    // slice overlap under the window edge
    float dy = 2.f * s.scale;            // light from above

    if (in * 2.f > size.x) {
        in = size.x / 2.f;
    }

    if (in * 2.f > size.y) {
        in = size.y / 2.f;
    }

    float xs[4] = {pos.x - m, pos.x + in, pos.x + size.x - in, pos.x + size.x + m};
    float ys[4] = {pos.y - m + dy, pos.y + in + dy, pos.y + size.y - in + dy, pos.y + size.y + m + dy};

    float cut0 = (float)(kMargin + kCore / 2) / kSprite;
    float cut1 = 1.f - cut0;
    float us[4] = {r.uv0.x, r.uv0.x + (r.uv1.x - r.uv0.x) * cut0, r.uv0.x + (r.uv1.x - r.uv0.x) * cut1, r.uv1.x};
    float vs[4] = {r.uv0.y, r.uv0.y + (r.uv1.y - r.uv0.y) * cut0, r.uv0.y + (r.uv1.y - r.uv0.y) * cut1, r.uv1.y};

    ImU32 col = IM_COL32(0, 0, 0, kAlpha);

    dl->PushClipRectFullScreen();

    // 9-slice ring, the center cell is hidden under the window
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            if (i == 1 && j == 1) {
                continue;
            }

            dl->AddImage(s.atlas->TexRef, ImVec2(xs[i], ys[j]), ImVec2(xs[i + 1], ys[j + 1]), ImVec2(us[i], vs[j]), ImVec2(us[i + 1], vs[j + 1]), col);
        }
    }

    dl->PopClipRect();
}

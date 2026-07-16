#pragma once

#include <imgui.h>

// drop shadows under imgui windows, fed into the imway imgui fork hook
// (ImGuiIO::WindowShadowCallback). the sprite is a gaussian rounded blob
// baked into the font atlas as a custom rect: same texture as the rest of
// the ui so batching never breaks, and atlas repacks move the pixels
// themselves — only the uv is re-read on every draw (GetCustomRect contract)
struct ShadowSprite {
    ImFontAtlas* atlas = nullptr;
    int rectId = -1;   // ImFontAtlasRectId
    float scale = 1.f; // written by the renderer on rescale
};

// registers and rasterizes the sprite; call once after fonts are set up
void bakeWindowShadow(ImFontAtlas* atlas, ShadowSprite& s);

// the ImGuiWindowShadowCallback: 9-slices the sprite around the window
void drawWindowShadow(ImDrawList* dl, ImVec2 pos, ImVec2 size, float rounding, ImGuiWindowFlags flags, void* user);

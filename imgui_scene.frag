#version 450 core

layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform sampler2D sTexture;
layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

layout(push_constant) uniform PushConstant {
    vec2 scale;
    vec2 translate;
    int textureEncoding;
    float sdrWhiteNits;
} pc;

vec3 srgbToLinear(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.04045));
    vec3 a = c / 12.92;
    vec3 b = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(b, a, lo);
}

vec3 bt709ToBt2020(vec3 c) {
    return mat3(
        0.627404, 0.069097, 0.016391,
        0.329283, 0.919540, 0.088013,
        0.043313, 0.011362, 0.895595
    ) * c;
}

void main() {
    vec4 sampled = texture(sTexture, In.UV.st);
    vec3 tint709 = srgbToLinear(In.Color.rgb);
    vec3 color;

    if (pc.textureEncoding == 0) {
        // Ordinary Wayland buffers and compositor UI are SDR sRGB/BT.709.
        color = bt709ToBt2020(srgbToLinear(sampled.rgb) * tint709) * pc.sdrWhiteNits;
    } else {
        // Converted color-managed textures are already linear BT.2020.
        // Relative textures use 1.0 == SDR white; absolute ones contain nits.
        vec3 tint2020 = bt709ToBt2020(tint709);
        float scale = pc.textureEncoding == 1 ? pc.sdrWhiteNits : 1.0;
        color = sampled.rgb * tint2020 * scale;
    }

    fColor = vec4(color, sampled.a * In.Color.a);
}

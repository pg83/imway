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

vec3 pqEncode(vec3 nits) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    vec3 p = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * p) / (1.0 + c3 * p), vec3(m2));
}

void main() {
    vec4 sampled = texture(sTexture, In.UV.st);

    if (pc.textureEncoding == 2) {
        // The scanout screenshot is already BT.2020/PQ. Preserve its code
        // values exactly; only opacity is relevant for this opaque image.
        fColor = vec4(sampled.rgb, sampled.a * In.Color.a);
        return;
    }

    // ImGui colors and its font atlas are SDR/sRGB. Since color management is
    // attached to the whole wl_surface, encode the UI into that surface's PQ
    // signal while keeping its white at the compositor's SDR white level.
    vec3 tint = srgbToLinear(In.Color.rgb);
    vec3 linear709 = srgbToLinear(sampled.rgb) * tint;
    vec3 nits2020 = bt709ToBt2020(linear709) * pc.sdrWhiteNits;
    fColor = vec4(pqEncode(nits2020), sampled.a * In.Color.a);
}

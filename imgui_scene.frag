#version 450 core

layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform sampler2D sTexture;
layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

layout(push_constant) uniform PushConstant {
    vec2 scale;
    vec2 translate;
    int textureSource;
    float sdrWhiteNits;
    int texturePrimaries;
    float textureReferenceNits;
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

vec3 pqEotf(vec3 e) {
    const float m1 = 0.1593017578125, m2 = 78.84375;
    const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    vec3 p = pow(max(e, 0.0), vec3(1.0 / m2));
    return pow(max(p - c1, 0.0) / (c2 - c3 * p), vec3(1.0 / m1)) * 10000.0;
}

vec3 hlgEotf(vec3 e, int primaries) {
    const float a = 0.17883277;
    const float b = 0.28466892;
    const float c = 0.55991073;
    bvec3 low = lessThanEqual(e, vec3(0.5));
    vec3 scene = mix((exp((e - c) / a) + b) / 12.0, e * e / 3.0, low);
    vec3 luma = primaries == 1
        ? vec3(0.2627, 0.6780, 0.0593)
        : vec3(0.2126, 0.7152, 0.0722);
    float y = max(dot(scene, luma), 0.0);
    return scene * (1000.0 * pow(y, 0.2));
}

void main() {
    vec4 sampled = texture(sTexture, In.UV.st);
    vec3 tint709 = srgbToLinear(In.Color.rgb);
    vec3 color;

    if (pc.textureSource == 0) {
        // ImGui textures use straight SDR sRGB/BT.709.
        color = bt709ToBt2020(srgbToLinear(sampled.rgb) * tint709) * pc.sdrWhiteNits;
    } else if (pc.textureSource == 2) {
        // Renderer-owned FP16 textures (lock-screen blur) are already
        // straight linear BT.2020 in absolute nits.
        color = sampled.rgb * bt709ToBt2020(tint709);
    } else {
        // Wayland RGB is premultiplied after transfer encoding. Decode the
        // straight electrical value inline; fixed-function SRC_ALPHA performs
        // the only premultiplication, directly into the FP16 scene.
        vec3 straight = sampled.a > 0.0
            ? clamp(sampled.rgb / sampled.a, 0.0, 1.0)
            : vec3(0.0);
        color = pc.textureSource == 4 ? pqEotf(straight) :
                pc.textureSource == 5 ? hlgEotf(straight, pc.texturePrimaries) :
                srgbToLinear(straight);

        if (pc.texturePrimaries == 0) {
            color = bt709ToBt2020(color);
        }

        vec3 tint2020 = bt709ToBt2020(tint709);
        float scale = pc.textureSource == 1
            ? (pc.textureReferenceNits > 0.0 ? pc.textureReferenceNits : pc.sdrWhiteNits)
            : 1.0;
        color *= tint2020 * scale;
    }

    fColor = vec4(color, sampled.a * In.Color.a);
}

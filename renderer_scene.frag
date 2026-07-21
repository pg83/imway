#version 450 core

layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform sampler2D sTexture;
layout(set = 0, binding = 1) uniform sampler2D sChroma;
layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

layout(push_constant) uniform PushConstant {
    vec2 scale;
    vec2 translate;
    int textureSource;
    float sdrWhiteNits;
    int texturePrimaries;
    float textureReferenceNits;
    float textureMinNits;
    float textureMaxNits;
    float p00; float p01; float p02;
    float p10; float p11; float p12;
    float p20; float p21; float p22;
    float gammaR; float gammaG; float gammaB;
    int alphaMode;
    int yuvCoefficients;
    int yuvRange;
    int yuvChromaLocation;
    int yuvBits;
} pc;

vec3 srgbToLinear(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.04045));
    vec3 a = c / 12.92;
    vec3 b = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(b, a, lo);
}

vec3 textureToBt2020(vec3 c) {
    return vec3(
        pc.p00 * c.r + pc.p01 * c.g + pc.p02 * c.b,
        pc.p10 * c.r + pc.p11 * c.g + pc.p12 * c.b,
        pc.p20 * c.r + pc.p21 * c.g + pc.p22 * c.b
    );
}

vec3 bt709ToBt2020(vec3 c) {
    return vec3(
        0.627404 * c.r + 0.329283 * c.g + 0.043313 * c.b,
        0.069097 * c.r + 0.919540 * c.g + 0.011362 * c.b,
        0.016391 * c.r + 0.088013 * c.g + 0.895595 * c.b
    );
}

vec3 pqEotf(vec3 e) {
    const float m1 = 0.1593017578125, m2 = 78.84375;
    const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    vec3 p = pow(max(e, 0.0), vec3(1.0 / m2));
    return pow(max(p - c1, 0.0) / (c2 - c3 * p), vec3(1.0 / m1)) * 10000.0;
}

vec3 hlgInverseOetf(vec3 e) {
    const float a = 0.17883277;
    const float b = 0.28466892;
    const float c = 0.55991073;
    bvec3 low = lessThanEqual(e, vec3(0.5));
    vec3 scene = mix((exp((e - c) / a) + b) / 12.0, e * e / 3.0, low);
    return scene;
}

vec3 bt1886Eotf(vec3 e) {
    const float gamma = 2.4;
    vec3 black = vec3(pow(max(pc.textureMinNits, 0.0), 1.0 / gamma));
    vec3 white = vec3(pow(max(pc.textureMaxNits, pc.textureMinNits), 1.0 / gamma));
    return pow(max(e * (white - black) + black, 0.0), vec3(gamma));
}

vec2 chromaUv(vec2 uv) {
    vec2 lumaSize = vec2(textureSize(sTexture, 0));
    vec2 chromaSize = vec2(textureSize(sChroma, 0));
    float xOffset = pc.yuvChromaLocation == 2 ||
                    pc.yuvChromaLocation == 4 ||
                    pc.yuvChromaLocation == 6 ? 0.5 : 0.0;
    float yOffset = pc.yuvChromaLocation <= 2 ? 0.5 :
                    pc.yuvChromaLocation <= 4 ? 0.0 : 1.0;
    vec2 lumaPosition = uv * lumaSize - 0.5;

    return ((lumaPosition - vec2(xOffset, yOffset)) * 0.5 + 0.5) /
           chromaSize;
}

vec3 sampleYuv(vec2 uv) {
    float y = texture(sTexture, uv).r;
    vec2 cbcr = texture(sChroma, chromaUv(uv)).rg;
    float maxCode = pc.yuvBits == 10 ? 1023.0 : 255.0;

    if (pc.yuvBits == 10) {
        // P010 stores each ten-bit code in the high bits of a 16-bit word.
        y *= 65535.0 / (64.0 * 1023.0);
        cbcr *= 65535.0 / (64.0 * 1023.0);
    }

    if (pc.yuvRange == 2) {
        float scale = pc.yuvBits == 10 ? 4.0 : 1.0;

        y = (y - 16.0 * scale / maxCode) /
            (219.0 * scale / maxCode);
        cbcr = (cbcr - vec2(128.0 * scale / maxCode)) /
               (224.0 * scale / maxCode);
    } else {
        cbcr -= vec2((pc.yuvBits == 10 ? 512.0 : 128.0) / maxCode);
    }

    float kr = pc.yuvCoefficients == 4 ? 0.299 :
               pc.yuvCoefficients == 6 ? 0.2627 : 0.2126;
    float kb = pc.yuvCoefficients == 4 ? 0.114 :
               pc.yuvCoefficients == 6 ? 0.0593 : 0.0722;
    float r = y + 2.0 * (1.0 - kr) * cbcr.y;
    float b = y + 2.0 * (1.0 - kb) * cbcr.x;
    float g = (y - kr * r - kb * b) / (1.0 - kr - kb);

    return vec3(r, g, b);
}

void main() {
    vec4 sampled = texture(sTexture, In.UV.st);

    if (pc.yuvCoefficients != 0) {
        sampled = vec4(sampleYuv(In.UV.st), 1.0);
    }
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
        vec3 straight = pc.alphaMode == 2 ? sampled.rgb :
            sampled.a > 0.0 ? sampled.rgb / sampled.a : vec3(0.0);

        if (pc.alphaMode == 1) {
            straight = sampled.rgb;
        }

        if (pc.textureSource != 6) {
            straight = clamp(straight, 0.0, 1.0);
        }

        color = pc.textureSource == 4 ? pqEotf(straight) :
                pc.textureSource == 5 ? hlgInverseOetf(straight) :
                pc.textureSource == 6 ? straight :
                pc.textureSource == 7 ? bt1886Eotf(straight) :
                pc.textureSource == 8 ? pow(max(straight, 0.0), vec3(2.2)) :
                pc.textureSource == 9 ? pow(max(straight, 0.0), vec3(pc.gammaR, pc.gammaG, pc.gammaB)) :
                srgbToLinear(straight);

        color = textureToBt2020(color);

        if (pc.alphaMode == 1 && sampled.a > 0.0) {
            color /= sampled.a;
        }

        if (pc.textureSource == 5) {
            float y = max(dot(color, vec3(0.2627, 0.6780, 0.0593)), 0.0);
            color *= 1000.0 * pow(y, 0.2);
        }

        vec3 tint2020 = bt709ToBt2020(tint709);
        float scale = pc.textureSource == 6
            ? pc.textureReferenceNits
            : pc.textureSource == 1 || pc.textureSource == 8 || pc.textureSource == 9
            ? (pc.textureReferenceNits > 0.0 ? pc.textureReferenceNits : pc.sdrWhiteNits)
            : 1.0;
        color *= tint2020 * scale;
    }

    fColor = vec4(color, sampled.a * In.Color.a);
}

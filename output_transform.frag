#version 450 core

layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform sampler2D sceneImage;

layout(push_constant) uniform OutputPush {
    vec4 toTarget[3];
    vec4 fromTarget[3];
    vec4 mapping; // peak nits, SDR scale (0 = PQ, -1 = unit bypass)
    vec4 color;   // target luminance coefficients in yzw
} pc;

vec3 applyRows(vec4 rows[3], vec3 c) {
    return vec3(dot(rows[0].xyz, c), dot(rows[1].xyz, c), dot(rows[2].xyz, c));
}

vec3 linearToSrgb(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.0031308));
    vec3 a = c * 12.92;
    vec3 b = 1.055 * pow(max(c, 0.0), vec3(1.0 / 2.4)) - 0.055;
    return mix(b, a, lo);
}

vec3 pqOetf(vec3 nits) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    vec3 p = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * p) / (1.0 + c3 * p), vec3(m2));
}

float toneMap(float value, float peak) {
    value = max(value, 0.0);
    float knee = peak * 0.9;
    if (value <= knee) return value;
    float headroom = peak - knee;
    return peak - headroom * headroom / (headroom + value - knee);
}

vec3 displayMap(vec3 scene) {
    vec3 target = applyRows(pc.toTarget, scene);
    float luminance = dot(pc.color.yzw, target);
    float mappedLuminance = toneMap(luminance, pc.mapping.x);

    if (luminance > 1e-6) target *= mappedLuminance / luminance;
    else target = vec3(0.0);

    float chromaScale = 1.0;
    for (int i = 0; i < 3; i++) {
        if (target[i] < 0.0) {
            chromaScale = min(chromaScale,
                mappedLuminance / (mappedLuminance - target[i]));
        } else if (target[i] > pc.mapping.x) {
            chromaScale = min(chromaScale,
                (pc.mapping.x - mappedLuminance) / (target[i] - mappedLuminance));
        }
    }

    return vec3(mappedLuminance) +
           (target - vec3(mappedLuminance)) * clamp(chromaScale, 0.0, 1.0);
}

void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneImage, 0));
    vec3 nits2020 = texture(sceneImage, uv).rgb;

    if (pc.mapping.y < 0.0) {
        fColor = vec4(clamp(nits2020, 0.0, 1.0), 1.0);
        return;
    }

    vec3 target = displayMap(nits2020);

    if (pc.mapping.y == 0.0) {
        fColor = vec4(pqOetf(applyRows(pc.fromTarget, target)), 1.0);
    } else {
        fColor = vec4(linearToSrgb(clamp(target / pc.mapping.y, 0.0, 1.0)), 1.0);
    }
}

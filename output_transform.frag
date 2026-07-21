#version 450 core

layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform sampler2D sceneImage;

layout(push_constant) uniform OutputPush {
    int hdr;
    float sdrWhiteNits;
    float temperatureR;
    float temperatureG;
    float temperatureB;
} pc;

vec3 bt2020ToBt709(vec3 c) {
    return mat3(
         1.660491, -0.124550, -0.018151,
        -0.587641,  1.132900, -0.100579,
        -0.072850, -0.008349,  1.118730
    ) * c;
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

void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneImage, 0));
    vec3 temperature = vec3(pc.temperatureR, pc.temperatureG, pc.temperatureB);
    vec3 nits2020 = texture(sceneImage, uv).rgb * temperature;

    if (pc.hdr != 0) {
        fColor = vec4(pqOetf(nits2020), 1.0);
    } else {
        vec3 relative709 = bt2020ToBt709(nits2020) / max(pc.sdrWhiteNits, 1.0);
        fColor = vec4(linearToSrgb(clamp(relative709, 0.0, 1.0)), 1.0);
    }
}

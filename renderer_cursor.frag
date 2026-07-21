#version 450 core

// Cursor-plane encode pass (renderer.cpp rasterizeShape): the scene is
// premultiplied linear BT.2020 in [0,1]; the plane wants premultiplied
// sRGB-encoded pixels with the alpha channel intact. No tone map, no dither.

layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform sampler2D sceneImage;

layout(push_constant) uniform CursorPush {
    vec4 toTarget[3];
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

void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneImage, 0));
    vec4 scene = texture(sceneImage, uv);
    vec3 straight = scene.a > 0.0 ? scene.rgb / scene.a : vec3(0.0);
    vec3 target = clamp(applyRows(pc.toTarget, straight), 0.0, 1.0);

    fColor = vec4(linearToSrgb(target) * scene.a, scene.a);
}

#version 450
// AION Surreal Boost - Spatial Upscale + Contrast Adaptive Sharpening (CAS)
layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uSource;

layout(push_constant) uniform PushConstants {
    vec2  invSrcSize;
    float sharpen;
    float pad;
} pc;

void main() {
    vec4 c = textureLod(uSource, vUV, 0.0);
    if (pc.sharpen <= 0.001) {
        outColor = c;
        return;
    }

    vec3 n = textureLod(uSource, vUV + vec2( 0.0,           -pc.invSrcSize.y), 0.0).rgb;
    vec3 s = textureLod(uSource, vUV + vec2( 0.0,            pc.invSrcSize.y), 0.0).rgb;
    vec3 w = textureLod(uSource, vUV + vec2(-pc.invSrcSize.x, 0.0           ), 0.0).rgb;
    vec3 e = textureLod(uSource, vUV + vec2( pc.invSrcSize.x, 0.0           ), 0.0).rgb;

    vec3 mnRGB = min(c.rgb, min(min(n, s), min(w, e)));
    vec3 mxRGB = max(c.rgb, max(max(n, s), max(w, e)));

    // Contrast-adaptive headroom: prevents haloing/clipping on high-contrast UI/edges
    vec3 ampRGB = clamp(min(mnRGB, 1.0 - mxRGB) / max(mxRGB, vec3(1e-5)), 0.0, 1.0);
    ampRGB = sqrt(ampRGB);

    float peak = -1.0 / mix(8.0, 5.0, clamp(pc.sharpen, 0.0, 1.0));
    vec3 wRGB = ampRGB * peak;
    vec3 rcpWeight = 1.0 / (1.0 + 4.0 * wRGB);

    vec3 res = clamp((c.rgb + (n + s + w + e) * wRGB) * rcpWeight, 0.0, 1.0);
    outColor = vec4(res, c.a);
}


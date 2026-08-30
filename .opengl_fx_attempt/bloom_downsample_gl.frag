#version 450 core

// Bloom Downsample — COD 13-tap partial-Karis filter. OpenGL port.
in  vec2 fragUV;
out vec4 outColor;

layout(binding = 0) uniform sampler2D srcTex;

uniform vec2  uSrcTexelSize;   // 1.0 / sourceSize
uniform float uKaris;          // 0 or 1 — Karis average on first downsample

vec3 KarisAverage(vec3 a, vec3 b, vec3 c, vec3 d) {
    float wa = 1.0 / (1.0 + max(a.r, max(a.g, a.b)));
    float wb = 1.0 / (1.0 + max(b.r, max(b.g, b.b)));
    float wc = 1.0 / (1.0 + max(c.r, max(c.g, c.b)));
    float wd = 1.0 / (1.0 + max(d.r, max(d.g, d.b)));
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

void main() {
    vec2 ts = uSrcTexelSize;

    vec3 a = texture(srcTex, fragUV + ts * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(srcTex, fragUV + ts * vec2( 0.0, -2.0)).rgb;
    vec3 c = texture(srcTex, fragUV + ts * vec2( 2.0, -2.0)).rgb;

    vec3 d = texture(srcTex, fragUV + ts * vec2(-2.0,  0.0)).rgb;
    vec3 e = texture(srcTex, fragUV + ts * vec2( 0.0,  0.0)).rgb;
    vec3 f = texture(srcTex, fragUV + ts * vec2( 2.0,  0.0)).rgb;

    vec3 g = texture(srcTex, fragUV + ts * vec2(-2.0,  2.0)).rgb;
    vec3 h = texture(srcTex, fragUV + ts * vec2( 0.0,  2.0)).rgb;
    vec3 i = texture(srcTex, fragUV + ts * vec2( 2.0,  2.0)).rgb;

    vec3 j = texture(srcTex, fragUV + ts * vec2(-1.0, -1.0)).rgb;
    vec3 k = texture(srcTex, fragUV + ts * vec2( 1.0, -1.0)).rgb;
    vec3 l = texture(srcTex, fragUV + ts * vec2(-1.0,  1.0)).rgb;
    vec3 m = texture(srcTex, fragUV + ts * vec2( 1.0,  1.0)).rgb;

    vec3 downsample;
    if (uKaris > 0.5) {
        vec3 box0 = KarisAverage(a, b, d, e);
        vec3 box1 = KarisAverage(b, c, e, f);
        vec3 box2 = KarisAverage(d, e, g, h);
        vec3 box3 = KarisAverage(e, f, h, i);
        vec3 box4 = KarisAverage(j, k, l, m);
        downsample = box0 * 0.125 + box1 * 0.125 + box2 * 0.125 + box3 * 0.125 + box4 * 0.5;
    } else {
        downsample  = e * 0.125;
        downsample += (a + c + g + i) * 0.03125;
        downsample += (b + d + f + h) * 0.0625;
        downsample += (j + k + l + m) * 0.125;
    }

    outColor = vec4(downsample, 1.0);
}

#version 450 core

// Bloom Upsample — 9-tap tent filter. OpenGL port. The destination FBO
// uses additive blending (GL_ONE, GL_ONE), so this outputs the filtered
// source scaled by intensity.
in  vec2 fragUV;
out vec4 outColor;

layout(binding = 0) uniform sampler2D srcTex;

uniform vec2  uSrcTexelSize;  // 1.0 / sourceSize
uniform float uFilterRadius;  // scales the tent; typical 1.0
uniform float uIntensity;     // output multiplier for this level

void main() {
    vec2 ts = uSrcTexelSize * uFilterRadius;

    vec3 a = texture(srcTex, fragUV + vec2(-ts.x, -ts.y)).rgb;
    vec3 b = texture(srcTex, fragUV + vec2( 0.0,  -ts.y)).rgb;
    vec3 c = texture(srcTex, fragUV + vec2( ts.x, -ts.y)).rgb;

    vec3 d = texture(srcTex, fragUV + vec2(-ts.x,  0.0)).rgb;
    vec3 e = texture(srcTex, fragUV + vec2( 0.0,   0.0)).rgb;
    vec3 f = texture(srcTex, fragUV + vec2( ts.x,  0.0)).rgb;

    vec3 g = texture(srcTex, fragUV + vec2(-ts.x,  ts.y)).rgb;
    vec3 h = texture(srcTex, fragUV + vec2( 0.0,   ts.y)).rgb;
    vec3 i = texture(srcTex, fragUV + vec2( ts.x,  ts.y)).rgb;

    vec3 upsample = (e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) / 16.0;
    outColor = vec4(upsample * uIntensity, 1.0);
}

#version 450 core

// Bloom Threshold/Prefilter (UE4-style soft-knee). OpenGL port.
in  vec2 fragUV;
out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneHDR;

uniform float uThreshold;
uniform float uKnee;

void main() {
    vec3 hdr = texture(sceneHDR, fragUV).rgb;
    hdr = min(hdr, vec3(1000.0));

    float brightness = max(hdr.r, max(hdr.g, hdr.b));

    float kneeDelta = uKnee * uThreshold;
    float soft = brightness - uThreshold + kneeDelta;
    soft = clamp(soft, 0.0, 2.0 * kneeDelta);
    soft = soft * soft / (4.0 * kneeDelta + 1e-4);

    float contribution = max(soft, brightness - uThreshold);
    contribution /= max(brightness, 1e-4);

    outColor = vec4(hdr * contribution, 1.0);
}

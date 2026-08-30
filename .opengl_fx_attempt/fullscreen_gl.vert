#version 450 core

// Shared fullscreen-triangle vertex shader for OpenGL post-process passes.
// No vertex buffer required — call glDrawArrays(GL_TRIANGLES, 0, 3) with an
// empty VAO. UV.y=0 at bottom (OpenGL-native), matching the GBuffer the
// deferred geometry pass writes (see ssao_gl / lighting_pass_gl).
out vec2 fragUV;

void main() {
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 pos = positions[gl_VertexID];
    gl_Position = vec4(pos, 0.0, 1.0);
    fragUV = pos * 0.5 + 0.5;
}

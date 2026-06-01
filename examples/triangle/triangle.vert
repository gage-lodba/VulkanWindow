#version 450

// Classic vertex-buffer-free triangle: positions and colours are baked into
// the shader and indexed by gl_VertexIndex, so vkCmdDraw(3, ...) is all the
// host side needs. Keeps the example focused on the pipeline + render-callback
// path rather than buffer management.

layout(location = 0) out vec3 fragColor;

vec2 positions[3] = vec2[](
    vec2( 0.0, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5)
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}

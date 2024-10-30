#version 450

layout (location = 0) out vec4 color;

layout (location = 2) in VertexOutput {
    vec2 uv;
    flat float instance_index;
    vec4 color;
    vec3 world_position;
} vertex_output;

layout (set = 3, binding = 0) uniform CommonUniformBlock {
    float time;
    float instance_count;
    
    mat4 view_projection_matrix;
} common_uniforms;

layout (set = 3, binding = 1) uniform FragmentUniformBlock {
    vec3 light_direction;
} fragment_uniforms;

void main() {
    color = vec4(vertex_output.uv, 0.0, 1.0);

    vec4 clip_position = common_uniforms.view_projection_matrix * vec4(vertex_output.world_position, 1.0);
    gl_FragDepth = 1.0 - clip_position.z / clip_position.w;
}
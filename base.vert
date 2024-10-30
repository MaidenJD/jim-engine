#version 450

//attributes
layout (location = 0) in vec3 position;
layout (location = 1) in vec2 uv;

//output
layout (location = 2) out VertexOutput {
    vec2 uv;
    flat float instance_index;
    vec4 color;
    vec3 world_position;
} vertex_output;

//uniforms
layout (set = 1, binding = 0) uniform CommonUniformBlock {
    float time;
    float instance_count;

    mat4 view_projection_matrix;
} common_uniforms;

layout (set = 1, binding = 1) uniform VertexUniformBlock {
    mat4 inv_view_projection_matrix;
} vertex_uniforms;

layout (set = 1, binding = 2) uniform PerInstanceVertexUniformBlock {
    mat4 model_matrix;
} per_instance_vertex_uniforms;

void main() {
    vertex_output.instance_index = gl_InstanceIndex;
    vertex_output.uv = uv;

    vec4 position = vec4(position, 1);
    vertex_output.world_position = (per_instance_vertex_uniforms.model_matrix * position).xyz;

    gl_Position = common_uniforms.view_projection_matrix * per_instance_vertex_uniforms.model_matrix * position;
}
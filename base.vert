#version 460
#extension GL_ARB_shading_language_include : require

//attributes
layout (location = 0) in vec3 position;
layout (location = 1) in vec2 uv;
layout (location = 2) in vec3 normal;

//output
layout (location = 3) out VertexOutput {
    vec2 uv;
    vec3 world_normal;
    flat float instance_index;
    vec4 color;
    vec3 world_position;
} vertex_output;

#define COMMON_UNIFORM_BINDING_SET 1
#include "common_uniforms.glsl"
#include "vertex_uniforms.glsl"
#include "per_instance_vertex_uniforms.glsl"

void main() {
    vertex_output.instance_index = gl_InstanceIndex;
    vertex_output.uv = uv;
    vertex_output.world_normal = (per_instance_vertex_uniforms.model_rotation_matrix * vec4(normal.xyz, 1)).xyz;

    vec4 position = vec4(position, 1);
    vertex_output.world_position = (per_instance_vertex_uniforms.model_matrix * position).xyz;

    gl_Position = common_uniforms.view_projection_matrix * per_instance_vertex_uniforms.model_matrix * position;
}
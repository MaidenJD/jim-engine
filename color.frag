#version 460
#extension GL_ARB_shading_language_include : require

layout (location = 0) out vec4 color;

layout (location = 3) in VertexOutput {
    vec2 uv;
    vec3 world_normal;
    flat float instance_index;
    vec4 color;
    vec3 world_position;
} vertex_output;

#define COMMON_UNIFORM_BINDING_SET 3
#include "common_uniforms.glsl"
#include "fragment_uniforms.glsl"

void main() {
    float light_intensity = dot(vertex_output.world_normal, -fragment_uniforms.light_direction) + 1 * 0.5;
    vec3 tint = vec3(0.7, 0.4, 0.3);

    color = vec4(light_intensity.rrr * tint, 1.0);

    vec4 clip_position = common_uniforms.view_projection_matrix * vec4(vertex_output.world_position, 1.0);
    gl_FragDepth = 1.0 - clip_position.z / clip_position.w;
}
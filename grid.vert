#version 460
#extension GL_ARB_shading_language_include : require

//output
layout (location = 1) out VertexOutput {
    vec3 near;
    vec3 far;

    mat4 view_projection_matrix;
} vertex_output;

#define COMMON_UNIFORM_BINDING_SET 1
#include "common_uniforms.glsl"
#include "vertex_uniforms.glsl"
#include "per_instance_vertex_uniforms.glsl"

vec3 deproject_point(vec3 point) {
    vec4 deprojected_point = vertex_uniforms.inv_view_projection_matrix * vec4(point, 1.0);
    return deprojected_point.xyz / deprojected_point.w;
}

void main() {
    const vec3 positions[] = {
        vec3(-1, -1, 0),
        vec3( 1, -1, 0),
        vec3( 1,  1, 0),
        vec3( 1,  1, 0),
        vec3(-1,  1, 0),
        vec3(-1, -1, 0),
    };

    vec4 position = vec4(positions[gl_VertexIndex], 1);
    vertex_output.near = deproject_point(vec3(position.xy, 0.0));
    vertex_output.far  = deproject_point(vec3(position.xy, 1.0));

    gl_Position = position;
}
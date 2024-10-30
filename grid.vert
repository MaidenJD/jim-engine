#version 450

//output
layout (location = 1) out VertexOutput {
    vec3 near;
    vec3 far;

    mat4 view_projection_matrix;
} vertex_output;

//uniforms
layout (set = 1, binding = 0) uniform CommonUniformBlock {
    float time;
    float instance_count;
} common_uniforms;

layout (set = 1, binding = 1) uniform VertexUniformBlock {
    mat4 inv_view_projection_matrix;
} vertex_uniforms;

layout (set = 1, binding = 2) uniform PerInstanceVertexUniformBlock {
    mat4 model_matrix;
} per_instance_vertex_uniforms;

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
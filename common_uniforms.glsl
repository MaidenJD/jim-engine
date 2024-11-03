layout (set = COMMON_UNIFORM_BINDING_SET, binding = 0) uniform CommonUniformBlock {
    float time;
    float instance_count;

    mat4 view_matrix;
    mat4 projection_matrix;
    mat4 view_projection_matrix;

    mat4 inv_view_matrix;
    mat4 inv_projection_matrix;
} common_uniforms;
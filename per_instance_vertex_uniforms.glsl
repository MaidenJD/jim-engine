layout (set = 1, binding = 2) uniform PerInstanceVertexUniformBlock {
    mat4 model_matrix;
    mat4 model_rotation_matrix;
} per_instance_vertex_uniforms;

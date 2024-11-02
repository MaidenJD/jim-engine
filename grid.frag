#version 450

// Grid - based on the work of Ben Golus. (https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8#1e7c)
// tweaked from there for multiple grid scales, and major grid lines

layout (location = 0) out vec4 out_color;

layout (location = 1) in VertexOutput {
    vec3 near;
    vec3 far;
} vertex_output;

layout (set = 3, binding = 0) uniform CommonUniformBlock {
    float time;
    float instance_count;

    mat4 view_projection_matrix;
} common_uniforms;

layout (set = 3, binding = 1) uniform FragmentUniformBlock {
    vec3 light_direction;
} fragment_uniforms;

float grid(vec2 uv, float line_width, float grid_scale) {
    uv *= grid_scale > 0.0 ? 1.0 / grid_scale : 0.0;
    line_width = clamp(0.01, 0.0, 1.0);

    bool invert_line = (line_width > 0.5);
    float target_width = invert_line ? 1.0 - line_width : line_width;

    vec4 uv_ddxy = vec4(dFdx(uv), dFdy(uv));
    vec2 uv_derivative = vec2(length(uv_ddxy.xz), length(uv_ddxy.yw));

    vec2 draw_width = clamp(target_width.xx, uv_derivative, 0.5.xx);

    vec2 line_aa = uv_derivative * 1.5;
    vec2 grid_uv = abs(fract(uv) * 2.0 - 1.0);
    grid_uv = vec2(
        invert_line ? grid_uv.r : 1.0 - grid_uv.r,
        invert_line ? grid_uv.g : 1.0 - grid_uv.g);

    vec2 grid_mask = smoothstep(draw_width + line_aa, draw_width - line_aa, grid_uv);
    grid_mask *= clamp(target_width / draw_width, 0.0.rr, 1.0.rr);
    grid_mask = mix(grid_mask, target_width.xx, clamp(uv_derivative * 2.0 - 1.0, 0.0.rr, 1.0.rr));
    grid_mask = vec2(
        invert_line ? 1.0 - grid_mask.r : grid_mask.r,
        invert_line ? 1.0 - grid_mask.g : grid_mask.g);

    return mix(grid_mask.x, 1.0, grid_mask.y);
}

void main() {
    float t = -vertex_output.near.y / (vertex_output.far.y - vertex_output.near.y);
    vec3 world_position = vertex_output.near + t * (vertex_output.far - vertex_output.near);
;
    vec2 uv = world_position.xz;
    float grid_mask = grid(uv, 0.01, 0.1);
    grid_mask = mix(grid_mask, 1.0, grid(uv, 0.01, 1));
    grid_mask = mix(grid_mask, 1.0, grid(uv, 0.01, 10));

    out_color = vec4(0.5.rrr, grid_mask * step(0, t));
    out_color.rgb = mix(out_color.rgb, vec3(0.3, 0.3, 0.8), (abs(world_position.x) < 0.1).rrr);
    out_color.rgb = mix(out_color.rgb, vec3(0.8, 0.3, 0.3), (abs(world_position.z) < 0.1).rrr);

    vec4 clip_position = common_uniforms.view_projection_matrix * vec4(world_position, 1.0);
    gl_FragDepth = 1.0 - mix(1.0, clip_position.z / clip_position.w, grid_mask * step(0, t));
}
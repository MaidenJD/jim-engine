#include <stddef.h>
#include <string.h>

#define SDL_MAIN_USE_CALLBACKS 1
#include "SDL3/SDL_main.h"
#include "SDL3/SDL.h"

#include "HandmadeMath.h"

#include "objzero.h"

#define NS_PER_UPDATE (1.0 / 60.0 * SDL_NS_PER_SECOND)

typedef struct CommonUniformBlock {
    float time;
    float instance_count;

    HMM_Mat4 view_projection_matrix;
} CommonUniformBlock;

typedef struct VertexUniformBlock {
    HMM_Mat4 inv_view_projection_matrix;
} VertexUniformBlock;

typedef struct PerInstanceVertexUniformBlock {
    HMM_Mat4 model_matrix;
} PerInstanceVertexUniformBlock;

typedef struct FragmentUniformBlock {
    HMM_Vec3 light_direction;
} FragmentUniformBlock;

typedef enum InputMode {
    INPUT_MODE_NONE,
    INPUT_MODE_CAMERA,
} InputMode;

typedef struct Transform {
    HMM_Vec3 location;
    HMM_Quat rotation;
    HMM_Vec3 scale;
} Transform;

const Transform DEFAULT_TRANSFORM = {
    .location = {0, 0, 0},
    .rotation = {0, 0, 0, 1},
    .scale    = {1, 1, 1},
};

HMM_Mat4 CalcTransformMatrix(Transform transform) {
    HMM_Mat4 result = HMM_M4D(1);
    result = HMM_MulM4(HMM_Scale(transform.scale), result);
    result = HMM_MulM4(HMM_QToM4(transform.rotation), result);
    result = HMM_MulM4(HMM_Translate(transform.location), result);
    return result;
}

Transform CalcMatrixTransform(HMM_Mat4 matrix) {
    Transform result = DEFAULT_TRANSFORM;
    result.location = matrix.Columns[3].XYZ;

    result.scale = HMM_V3(
        HMM_LenV3(matrix.Columns[0].XYZ),
        HMM_LenV3(matrix.Columns[1].XYZ),
        HMM_LenV3(matrix.Columns[2].XYZ)
    );

    matrix.Columns[0].XYZ = HMM_DivV3F(matrix.Columns[0].XYZ, result.scale.X);
    matrix.Columns[1].XYZ = HMM_DivV3F(matrix.Columns[1].XYZ, result.scale.Y);
    matrix.Columns[2].XYZ = HMM_DivV3F(matrix.Columns[2].XYZ, result.scale.Z);
    matrix.Columns[3].XYZ = HMM_V3(0, 0, 0);

    result.rotation = HMM_M4ToQ_RH(matrix);

    return result;
}

typedef struct Entity {
    Transform transform;
} Entity;

typedef struct Camera {
    Entity base;

    float fov;
    float turn_rate;
    float pitch_rate;
    float movement_speed;
} Camera;

void InitCamera(Camera *camera) {
    Transform t = DEFAULT_TRANSFORM;
    t.location.Z = 5;
    t.location.Y = 1;

    camera->base.transform = t;
    camera->fov = 90.0f;
    camera->turn_rate = 30.0f;
    camera->pitch_rate = 30.0f;
    camera->movement_speed = 3.0f;
}

typedef struct AppState {
    bool is_valid;
    Uint64 nanoseconds_since_init;
    Uint64 nanoseconds_update_lag;

    InputMode input_mode;

    Camera camera;

    SDL_GPUFence *render_fence;

    CommonUniformBlock common_uniforms;
    VertexUniformBlock vertex_uniforms;
    PerInstanceVertexUniformBlock per_instance_vertex_uniforms;
    VertexUniformBlock fragment_uniforms;
    // PerInstanceFragmentUniformBlock per_instance_fragment_uniforms;

    SDL_GPUTexture *depth_texture;

    SDL_Window    *window;
    SDL_GPUDevice *gpu;

    SDL_GPUGraphicsPipeline *grid_pipeline;

    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *index_buffer;
    Uint32 num_indices;

    SDL_GPUGraphicsPipeline *mesh_pipeline;
} AppState;

typedef struct VertexLayout {
    HMM_Vec3 position;
    HMM_Vec2 uv;
} VertexLayout;

SDL_GPUShader *create_shader_from_file(SDL_GPUDevice *gpu, const char *filename, SDL_GPUShaderStage stage, Uint32 num_samplers, Uint32 num_storage_buffers, Uint32 num_storage_textures, Uint32 num_uniform_buffers) {
    SDL_IOStream *file_io = SDL_IOFromFile(filename, "rb");
    if (!file_io) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open \"%s\". %s", filename, SDL_GetError());
        return NULL;
    }

    Sint64 bytecode_size = SDL_GetIOSize(file_io);
    void *bytecode = SDL_malloc(bytecode_size);

    size_t num_bytes_read = SDL_ReadIO(file_io, bytecode, bytecode_size);
    SDL_CloseIO(file_io);

    if (num_bytes_read < bytecode_size) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to read shader bytecode from file \"%s\". %s", filename, SDL_GetError());
        return NULL;
    }

    SDL_GPUShaderCreateInfo descriptor = {
        .stage                  = stage,
        .format                 = SDL_GPU_SHADERFORMAT_SPIRV,
        .code                   = bytecode,
        .code_size              = bytecode_size,
        .entrypoint             = "main",
        .num_samplers           = num_samplers,
        .num_storage_buffers    = num_storage_buffers,
        .num_storage_textures   = num_storage_textures,
        .num_uniform_buffers    = num_uniform_buffers,
    };

    SDL_GPUShader *shader = SDL_CreateGPUShader(gpu, &descriptor);
    SDL_free(bytecode);

    if (!shader) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shader from file \"%s\". %s", filename, SDL_GetError());
        return NULL;
    }

    return shader;
}

bool create_grid_pipeline(AppState *app_state) {
    SDL_GPUShader *vertex_shader = create_shader_from_file(app_state->gpu, "grid.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0, 3);
    if (!vertex_shader) {
        return false;
    }

    SDL_GPUShader *fragment_shader = create_shader_from_file(app_state->gpu, "grid.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 2);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(app_state->gpu, vertex_shader);
        return false;
    }

    SDL_GPUGraphicsPipelineCreateInfo pipeline_descriptor = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info = {
            .num_color_targets = 1,
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]) {{
                .format = SDL_GetGPUSwapchainTextureFormat(app_state->gpu, app_state->window),
                .blend_state = {
                    .enable_blend = true,
                    .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
					.color_blend_op = SDL_GPU_BLENDOP_ADD,
					.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
					.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
					.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
					.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
                }
            }},
            .has_depth_stencil_target = true,
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        },
        .depth_stencil_state = {
            .enable_depth_test = true,
            .enable_depth_write = true,
            .compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL,
        },
        .rasterizer_state = {
            .cull_mode  = SDL_GPU_CULLMODE_NONE,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        },
    };

    app_state->grid_pipeline = SDL_CreateGPUGraphicsPipeline(app_state->gpu, &pipeline_descriptor);

    SDL_ReleaseGPUShader(app_state->gpu, fragment_shader);
    SDL_ReleaseGPUShader(app_state->gpu, vertex_shader);

    if (!app_state->grid_pipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grid pipeline. %s", SDL_GetError());
        return false;
    }

    return true;
}

bool create_mesh_pipeline(AppState *app_state) {
    SDL_GPUShader *vertex_shader = create_shader_from_file(app_state->gpu, "base.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0, 3);
    if (!vertex_shader) {
        return false;
    }

    SDL_GPUShader *fragment_shader = create_shader_from_file(app_state->gpu, "color.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 2);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(app_state->gpu, vertex_shader);
        return false;
    }

    SDL_GPUGraphicsPipelineCreateInfo pipeline_descriptor = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info = {
            .num_color_targets = 1,
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]) {{
                .format = SDL_GetGPUSwapchainTextureFormat(app_state->gpu, app_state->window),
                .blend_state = {
                    .enable_blend = true,
                    .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
					.color_blend_op = SDL_GPU_BLENDOP_ADD,
					.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
					.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
					.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
					.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                },
            }},
            .has_depth_stencil_target = true,
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        },
        .depth_stencil_state = {
            .enable_depth_test = true,
            .enable_depth_write = true,
            .compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL,
        },
        .vertex_input_state = {
            .num_vertex_buffers = 1,
            .vertex_buffer_descriptions = (SDL_GPUVertexBufferDescription[]) {
                {
                    .slot = 0,
                    .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
                    .pitch = sizeof(VertexLayout),
                }
            },
            .num_vertex_attributes = 2,
            .vertex_attributes = (SDL_GPUVertexAttribute[]) {
                {
                    .buffer_slot = 0,
                    .offset = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                    .location = 0,
                },
                {
                    .buffer_slot = 0,
                    .offset = sizeof(HMM_Vec3),
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                    .location = 1,
                }
            }
        },
        .rasterizer_state = {
            .cull_mode  = SDL_GPU_CULLMODE_BACK,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        },
    };

    app_state->mesh_pipeline = SDL_CreateGPUGraphicsPipeline(app_state->gpu, &pipeline_descriptor);

    SDL_ReleaseGPUShader(app_state->gpu, fragment_shader);
    SDL_ReleaseGPUShader(app_state->gpu, vertex_shader);

    if (!app_state->mesh_pipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create mesh pipeline. %s", SDL_GetError());
        return false;
    }

    return true;
}

void recreate_depth_texture(AppState *app_state) {
    if (app_state->depth_texture) {
        SDL_ReleaseGPUTexture(app_state->gpu, app_state->depth_texture);
        app_state->depth_texture = NULL;
    }

    int width, height;
    SDL_GetWindowSizeInPixels(app_state->window, &width, &height);

    SDL_GPUTextureCreateInfo depth_texture_descriptor = {
        .type   = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        .usage  = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width  = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };

    app_state->depth_texture = SDL_CreateGPUTexture(app_state->gpu, &depth_texture_descriptor);

    if (app_state->depth_texture) {
        SDL_SetGPUTextureName(app_state->gpu, app_state->depth_texture, "Depth Texture");
    }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    AppState *app_state = SDL_malloc(sizeof(AppState));
    SDL_zerop(app_state);
    *appstate = app_state;

    InitCamera(&app_state->camera);

    // {
    //     Transform t = DEFAULT_TRANSFORM;
    //     t.location.Z = 5;
    //     t.location.Y = 1;

    //     app_state->camera.base.transform = t;
    // }

    objz_setVertexFormat(sizeof(VertexLayout), offsetof(VertexLayout, position), offsetof(VertexLayout, uv), SIZE_MAX);
    objz_setIndexFormat(OBJZ_INDEX_FORMAT_U32);

    bool init_successful = SDL_Init(SDL_INIT_VIDEO);
    if (!init_successful) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to init SDL. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    app_state->gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, /*debug_mode =*/ true, /*name =*/ NULL);
    if (!app_state->gpu) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create gpu device. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Selected GPU backend \"%s\"", SDL_GetGPUDeviceDriver(app_state->gpu));

    app_state->window = SDL_CreateWindow("SDL3 Grid", 1280, 720, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!app_state->window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create main window. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    bool claimed_window_for_gpu = SDL_ClaimWindowForGPUDevice(app_state->gpu, app_state->window);
    if (!claimed_window_for_gpu) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to claim window for gpu device. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    recreate_depth_texture(app_state);

    if (!app_state->depth_texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth texture. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    objzModel *model = objz_load("models/suzanne.obj");

    SDL_GPUBufferCreateInfo vertex_buffer_descriptor = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(VertexLayout) * model->numVertices,
    };

    app_state->vertex_buffer = SDL_CreateGPUBuffer(app_state->gpu, &vertex_buffer_descriptor);
    if (!app_state->vertex_buffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create vertex buffer. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetGPUBufferName(app_state->gpu, app_state->vertex_buffer, "vertex_buffer");

    SDL_GPUBufferCreateInfo index_buffer_descriptor = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = sizeof(Uint32) * model->numIndices,
    };

    app_state->index_buffer = SDL_CreateGPUBuffer(app_state->gpu, &index_buffer_descriptor);
    if (!app_state->index_buffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create index buffer. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetGPUBufferName(app_state->gpu, app_state->index_buffer, "index_buffer");

    {
        SDL_GPUTransferBufferCreateInfo vertex_transfer_buffer_descriptor = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = sizeof(VertexLayout) * model->numVertices,
        };

        SDL_GPUTransferBuffer *vertex_transfer_buffer = SDL_CreateGPUTransferBuffer(app_state->gpu, &vertex_transfer_buffer_descriptor);
        if (!vertex_transfer_buffer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create transfer buffer. %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }

        {
            VertexLayout *vertices = SDL_MapGPUTransferBuffer(app_state->gpu, vertex_transfer_buffer, false);
            if (!vertices) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map transfer buffer. %s", SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(app_state->gpu, vertex_transfer_buffer);
                return SDL_APP_FAILURE;
            }

            memcpy(vertices, model->vertices, vertex_buffer_descriptor.size);

            SDL_UnmapGPUTransferBuffer(app_state->gpu, vertex_transfer_buffer);
        }

        app_state->num_indices = model->numIndices;

        SDL_GPUTransferBufferCreateInfo index_transfer_buffer_descriptor = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = sizeof(Uint32) * app_state->num_indices,
        };

        SDL_GPUTransferBuffer *index_transfer_buffer = SDL_CreateGPUTransferBuffer(app_state->gpu, &index_transfer_buffer_descriptor);
        if (!index_transfer_buffer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create transfer buffer. %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }

        {
            Uint32 *indices = SDL_MapGPUTransferBuffer(app_state->gpu, index_transfer_buffer, false);
            if (!indices) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map transfer buffer. %s", SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(app_state->gpu, index_transfer_buffer);
                return SDL_APP_FAILURE;
            }

            memcpy(indices, model->indices, index_buffer_descriptor.size);

            SDL_UnmapGPUTransferBuffer(app_state->gpu, index_transfer_buffer);
        }

        objz_destroy(model);

        SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(app_state->gpu);
        if (!command_buffer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to acquire command buffer. %s", SDL_GetError());
            SDL_ReleaseGPUTransferBuffer(app_state->gpu, vertex_transfer_buffer);
            SDL_ReleaseGPUTransferBuffer(app_state->gpu, index_transfer_buffer);
            return SDL_APP_FAILURE;
        }

        SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(command_buffer);

        SDL_GPUTransferBufferLocation source = {
            .transfer_buffer = vertex_transfer_buffer,
        };

        SDL_GPUBufferRegion destination = {
            .buffer = app_state->vertex_buffer,
            .size = vertex_buffer_descriptor.size,
        };

        SDL_UploadToGPUBuffer(pass, &source, &destination, false);

        source = (SDL_GPUTransferBufferLocation) {
            .transfer_buffer = index_transfer_buffer,
        };

        destination = (SDL_GPUBufferRegion) {
            .buffer = app_state->index_buffer,
            .size = index_buffer_descriptor.size,
        };

        SDL_UploadToGPUBuffer(pass, &source, &destination, false);

        SDL_EndGPUCopyPass(pass);

        bool submitted_command_buffer = SDL_SubmitGPUCommandBuffer(command_buffer);

        SDL_ReleaseGPUTransferBuffer(app_state->gpu, vertex_transfer_buffer);
        SDL_ReleaseGPUTransferBuffer(app_state->gpu, index_transfer_buffer);

        if (!submitted_command_buffer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to submit command buffer. %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
    }

    bool created_mesh_pipeline = create_mesh_pipeline(app_state);
    if (!created_mesh_pipeline) {
        return SDL_APP_FAILURE;
    }

    bool created_grid_pipeline = create_grid_pipeline(app_state);
    if (!created_grid_pipeline) {
        return SDL_APP_FAILURE;
    }

    bool updated_swapchain_parameters = SDL_SetGPUSwapchainParameters(app_state->gpu, app_state->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_IMMEDIATE);
    if (!updated_swapchain_parameters) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to update swapchain parameters. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    bool window_shown = SDL_ShowWindow(app_state->window);
    if (!window_shown) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to show main window. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    app_state->nanoseconds_since_init = SDL_GetTicksNS();
    app_state->is_valid = true;
    return SDL_APP_CONTINUE;
}

void UpdateCamera(AppState *app_state, Camera *camera, float dt) {
    if (app_state->input_mode == INPUT_MODE_CAMERA) {
        HMM_Vec2 mouse_delta;
        SDL_GetRelativeMouseState(&mouse_delta.X, &mouse_delta.Y);

        if (HMM_LenSqrV2(mouse_delta) > 0) {
            HMM_Mat4 rotation = HMM_QToM4(camera->base.transform.rotation);
            rotation = HMM_MulM4(HMM_Rotate_RH(-mouse_delta.X * HMM_DegToRad * camera->turn_rate * dt, HMM_V3(0, 1, 0)), rotation);
            rotation = HMM_MulM4(HMM_Rotate_RH(-mouse_delta.Y * HMM_DegToRad * camera->pitch_rate * dt, HMM_MulM4V4(rotation, HMM_V4(1, 0, 0, 0)).XYZ), rotation);
            camera->base.transform.rotation = HMM_M4ToQ_RH(rotation);
        }

        const bool *keys = SDL_GetKeyboardState(NULL);

        HMM_Vec3 movement_input = HMM_V3(0, 0, 0);
        if (keys[SDL_SCANCODE_W])     { movement_input = HMM_AddV3(movement_input, HMM_V3( 0,  0, -1)); }
        if (keys[SDL_SCANCODE_S])     { movement_input = HMM_AddV3(movement_input, HMM_V3( 0,  0,  1)); }
        if (keys[SDL_SCANCODE_A])     { movement_input = HMM_AddV3(movement_input, HMM_V3(-1,  0,  0)); }
        if (keys[SDL_SCANCODE_D])     { movement_input = HMM_AddV3(movement_input, HMM_V3( 1,  0,  0)); }
        if (keys[SDL_SCANCODE_LCTRL]) { movement_input = HMM_AddV3(movement_input, HMM_V3( 0, -1,  0)); }
        if (keys[SDL_SCANCODE_SPACE]) { movement_input = HMM_AddV3(movement_input, HMM_V3( 0,  1,  0)); }

        if (HMM_LenSqrV3(movement_input) > 0) {
            movement_input = HMM_NormV3(movement_input);
            HMM_Vec3 movement_delta = HMM_RotateV3Q(HMM_MulV3F(movement_input, dt * camera->movement_speed), camera->base.transform.rotation);

            HMM_Vec3 *location = &camera->base.transform.location;
            *location = HMM_AddV3(*location, movement_delta);
        }
    }
};

SDL_AppResult Update(AppState *app_state, float dt) {
    UpdateCamera(app_state, &app_state->camera, dt);

    return SDL_APP_CONTINUE;
}

SDL_AppResult Render(AppState *app_state) {
    //  Only render for first frame, when previous frame has finished;
    if (app_state->render_fence) {
        bool ready_for_new_frame = SDL_QueryGPUFence(app_state->gpu, app_state->render_fence);
        if (!ready_for_new_frame) {
            return SDL_APP_CONTINUE;
        }

        SDL_ReleaseGPUFence(app_state->gpu, app_state->render_fence);
    }

    float time = (SDL_GetTicksNS() / (double) SDL_NS_PER_SECOND);

    int width, height;
    if (!SDL_GetWindowSize(app_state->window, &width, &height)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to get window size. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    float aspect_ratio = width / (float) height;

    float phase = time * (HMM_PI * 2.0f) * 0.1f;

    HMM_Mat4 view_matrix = HMM_InvGeneralM4(CalcTransformMatrix(app_state->camera.base.transform)); //HMM_LookAt_RH(HMM_V3(/*HMM_CosF(phase) * 5.0f*/0, /*HMM_SinF(phase) * 5.0f*/1, /*HMM_SinF(phase) * 5.0f*/3), HMM_V3(0, 0, 0), HMM_V3(0, 1, 0));
    HMM_Mat4 projection_matrix = HMM_Perspective_RH_NO(app_state->camera.fov * HMM_DegToRad, aspect_ratio, 0.3f, 10000.0f);
    app_state->common_uniforms.view_projection_matrix = HMM_MulM4(projection_matrix, view_matrix);
    app_state->vertex_uniforms.inv_view_projection_matrix = HMM_InvGeneralM4(app_state->common_uniforms.view_projection_matrix);

    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(app_state->gpu);
    if (!command_buffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to acquire command buffer. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_GPUTexture *swapchain_texture = NULL;
    Uint32 swapchain_width  = 0;
    Uint32 swapchain_height = 0;

    bool acquired_swapchain_texture = SDL_AcquireGPUSwapchainTexture(command_buffer, app_state->window, &swapchain_texture, &swapchain_width, &swapchain_height);
    if (!acquired_swapchain_texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to acquire swapchain texture. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    int instance_count = 1;
    app_state->common_uniforms.instance_count = instance_count;
    app_state->common_uniforms.time = app_state->nanoseconds_since_init / (double) SDL_NS_PER_SECOND;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &app_state->common_uniforms, sizeof(CommonUniformBlock));
    SDL_PushGPUVertexUniformData(command_buffer, 1, &app_state->vertex_uniforms, sizeof(VertexUniformBlock));

    SDL_PushGPUFragmentUniformData(command_buffer, 0, &app_state->common_uniforms, sizeof(CommonUniformBlock));
    SDL_PushGPUFragmentUniformData(command_buffer, 1, &app_state->fragment_uniforms, sizeof(FragmentUniformBlock));
    //SDL_PushGPUFragmentUniformData(command_buffer, 2, &app_state->per_instance_fragment_uniforms, sizeof(PerInstanceFragmentUniformBlock));

    SDL_GPUColorTargetInfo clear_target_info = {
        .texture = swapchain_texture,
        .clear_color = { 0.2f, 0.2f, 0.25f, 1.0f },
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };

    SDL_GPUDepthStencilTargetInfo depth_target_info = {
        .texture = app_state->depth_texture,
        .clear_depth = 0.0f,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command_buffer, &clear_target_info, 1, &depth_target_info);
    if (pass) {
        SDL_BindGPUGraphicsPipeline(pass, app_state->grid_pipeline);
        app_state->per_instance_vertex_uniforms.model_matrix = HMM_M4D(1);
        SDL_PushGPUVertexUniformData(command_buffer, 2, &app_state->per_instance_vertex_uniforms, sizeof(PerInstanceVertexUniformBlock));
        SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);

        SDL_BindGPUGraphicsPipeline(pass, app_state->mesh_pipeline);
        SDL_BindGPUVertexBuffers(pass, 0, (SDL_GPUBufferBinding[]) {{.buffer = app_state->vertex_buffer}}, 1);
        SDL_BindGPUIndexBuffer(pass, &(SDL_GPUBufferBinding) {.buffer = app_state->index_buffer}, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        app_state->per_instance_vertex_uniforms.model_matrix = HMM_Rotate_RH(time * 45.0 * HMM_DegToRad, HMM_V3(0, 1, 0));
        SDL_PushGPUVertexUniformData(command_buffer, 2, &app_state->per_instance_vertex_uniforms, sizeof(PerInstanceVertexUniformBlock));
        SDL_DrawGPUIndexedPrimitives(pass, app_state->num_indices, instance_count, 0, 0, 0);

        SDL_EndGPURenderPass(pass);
    }

    app_state->render_fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command_buffer);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState *app_state = appstate;

    Uint64 nanoseconds_since_init = SDL_GetTicksNS();
    Uint64 nanosecond_delta = nanoseconds_since_init - app_state->nanoseconds_since_init;
    app_state->nanoseconds_since_init = nanoseconds_since_init;
    app_state->nanoseconds_update_lag += nanosecond_delta;

    SDL_PumpEvents();

    while (app_state->nanoseconds_update_lag >= NS_PER_UPDATE) {
        Update(app_state, NS_PER_UPDATE / (double) SDL_NS_PER_SECOND);
        app_state->nanoseconds_update_lag -= NS_PER_UPDATE;
    }

    Render(app_state);

    return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    AppState *app_state = appstate;

    if (!app_state->is_valid) {
        return SDL_APP_FAILURE;
    }

    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_Q) {
            return SDL_APP_SUCCESS;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            app_state->input_mode = INPUT_MODE_CAMERA;

            //  Needed to eat first delta check, to avoid camera snapping
            SDL_GetRelativeMouseState(NULL, NULL);

            SDL_SetWindowRelativeMouseMode(app_state->window, true);
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            app_state->input_mode = INPUT_MODE_NONE;

            SDL_SetWindowRelativeMouseMode(app_state->window, false);

            int w, h;
            SDL_GetWindowSize(app_state->window, &w, &h);
            HMM_Vec2 window_center = { w * 0.5f, h * 0.5f };

            SDL_WarpMouseInWindow(app_state->window, window_center.X, window_center.Y);
        }
    }

    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        return SDL_APP_SUCCESS;
    }

    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        recreate_depth_texture(app_state);
        if (!app_state->depth_texture) {
            return SDL_APP_FAILURE;
        }
    }

    return SDL_APP_CONTINUE;
}
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    AppState *app_state = appstate;

    SDL_WaitForGPUIdle(app_state->gpu);

    if (app_state->render_fence) {
        SDL_ReleaseGPUFence(app_state->gpu, app_state->render_fence);
    }

    if (app_state->mesh_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(app_state->gpu, app_state->mesh_pipeline);
    }

    if (app_state->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(app_state->gpu, app_state->grid_pipeline);
    }

    if (app_state->depth_texture) {
        SDL_ReleaseGPUTexture(app_state->gpu, app_state->depth_texture);
    }

    if (app_state->vertex_buffer) {
        SDL_ReleaseGPUBuffer(app_state->gpu, app_state->vertex_buffer);
    }

    if (app_state->index_buffer) {
        SDL_ReleaseGPUBuffer(app_state->gpu, app_state->index_buffer);
    }

    if (app_state->window) {
        SDL_ReleaseWindowFromGPUDevice(app_state->gpu, app_state->window);
        SDL_DestroyWindow(app_state->window);
    }

    if (app_state->gpu) {
        SDL_DestroyGPUDevice(app_state->gpu);
    }

    SDL_Quit();

    SDL_free(app_state);
}
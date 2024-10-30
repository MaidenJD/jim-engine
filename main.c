#define SDL_MAIN_USE_CALLBACKS 1
#include "SDL3/SDL_main.h"
#include "SDL3/SDL.h"

#include "HandmadeMath.h"

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

typedef struct AppState {
    bool is_valid;
    Uint64 nanoseconds_since_init;

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
    SDL_GPUGraphicsPipeline *mesh_pipeline;
} AppState;

typedef struct VertexLayout {
    HMM_Vec3 position;
    HMM_Vec2 uv;
} VertexLayout;

static AppState app_state = { 0 };

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

bool create_grid_pipeline() {
    SDL_GPUShader *vertex_shader = create_shader_from_file(app_state.gpu, "grid.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0, 3);
    if (!vertex_shader) {
        return false;
    }

    SDL_GPUShader *fragment_shader = create_shader_from_file(app_state.gpu, "grid.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 2);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(app_state.gpu, vertex_shader);
        return false;
    }

    SDL_GPUGraphicsPipelineCreateInfo pipeline_descriptor = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info = {
            .num_color_targets = 1,
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]) {{
                .format = SDL_GetGPUSwapchainTextureFormat(app_state.gpu, app_state.window),
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

    app_state.grid_pipeline = SDL_CreateGPUGraphicsPipeline(app_state.gpu, &pipeline_descriptor);

    SDL_ReleaseGPUShader(app_state.gpu, fragment_shader);
    SDL_ReleaseGPUShader(app_state.gpu, vertex_shader);

    if (!app_state.grid_pipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grid pipeline. %s", SDL_GetError());
        return false;
    }

    return true;
}

bool create_mesh_pipeline() {
    SDL_GPUShader *vertex_shader = create_shader_from_file(app_state.gpu, "base.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0, 3);
    if (!vertex_shader) {
        return false;
    }

    SDL_GPUShader *fragment_shader = create_shader_from_file(app_state.gpu, "color.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 2);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(app_state.gpu, vertex_shader);
        return false;
    }

    SDL_GPUGraphicsPipelineCreateInfo pipeline_descriptor = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info = {
            .num_color_targets = 1,
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]) {{
                .format = SDL_GetGPUSwapchainTextureFormat(app_state.gpu, app_state.window),
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

    app_state.mesh_pipeline = SDL_CreateGPUGraphicsPipeline(app_state.gpu, &pipeline_descriptor);

    SDL_ReleaseGPUShader(app_state.gpu, fragment_shader);
    SDL_ReleaseGPUShader(app_state.gpu, vertex_shader);

    if (!app_state.mesh_pipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create mesh pipeline. %s", SDL_GetError());
        return false;
    }

    return true;
}

void recreate_depth_texture() {
    if (app_state.depth_texture) {
        SDL_ReleaseGPUTexture(app_state.gpu, app_state.depth_texture);
        app_state.depth_texture = NULL;
    }

    int width, height;
    SDL_GetWindowSizeInPixels(app_state.window, &width, &height);

    SDL_GPUTextureCreateInfo depth_texture_descriptor = {
        .type   = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        .usage  = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width  = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };

    app_state.depth_texture = SDL_CreateGPUTexture(app_state.gpu, &depth_texture_descriptor);

    if (app_state.depth_texture) {
        SDL_SetGPUTextureName(app_state.gpu, app_state.depth_texture, "Depth Texture");
    }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    bool init_successful = SDL_Init(SDL_INIT_VIDEO);
    if (!init_successful) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to init SDL. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    app_state.gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, /*debug_mode =*/ true, /*name =*/ NULL);
    if (!app_state.gpu) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create gpu device. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Selected GPU backend \"%s\"", SDL_GetGPUDeviceDriver(app_state.gpu));

    app_state.window = SDL_CreateWindow("SDL3 Grid", 1280, 720, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!app_state.window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create main window. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    bool claimed_window_for_gpu = SDL_ClaimWindowForGPUDevice(app_state.gpu, app_state.window);
    if (!claimed_window_for_gpu) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to claim window for gpu device. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    recreate_depth_texture();

    if (!app_state.depth_texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth texture. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_GPUBufferCreateInfo vertex_buffer_descriptor = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(VertexLayout) * 8,
    };

    app_state.vertex_buffer = SDL_CreateGPUBuffer(app_state.gpu, &vertex_buffer_descriptor);
    if (!app_state.vertex_buffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create vertex buffer. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetGPUBufferName(app_state.gpu, app_state.vertex_buffer, "vertex_buffer");

    SDL_GPUBufferCreateInfo index_buffer_descriptor = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = sizeof(Uint32) * 36,
    };

    app_state.index_buffer = SDL_CreateGPUBuffer(app_state.gpu, &index_buffer_descriptor);
    if (!app_state.index_buffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create index buffer. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetGPUBufferName(app_state.gpu, app_state.index_buffer, "index_buffer");

    {
        SDL_GPUTransferBufferCreateInfo vertex_transfer_buffer_descriptor = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = sizeof(VertexLayout) * 8
        };

        SDL_GPUTransferBuffer *vertex_transfer_buffer = SDL_CreateGPUTransferBuffer(app_state.gpu, &vertex_transfer_buffer_descriptor);
        if (!vertex_transfer_buffer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create transfer buffer. %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }

        {
            VertexLayout *vertices = SDL_MapGPUTransferBuffer(app_state.gpu, vertex_transfer_buffer, false);
            if (!vertices) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map transfer buffer. %s", SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(app_state.gpu, vertex_transfer_buffer);
                return SDL_APP_FAILURE;
            }

            vertices[0] = (VertexLayout) {{-1, -1, -1}, {0, 0}}; // Vertex 0
            vertices[1] = (VertexLayout) {{ 1, -1, -1}, {1, 0}}; // Vertex 1
            vertices[2] = (VertexLayout) {{ 1,  1, -1}, {1, 1}}; // Vertex 2
            vertices[3] = (VertexLayout) {{-1,  1, -1}, {0, 1}}; // Vertex 3
            vertices[4] = (VertexLayout) {{-1, -1,  1}, {0, 0}}; // Vertex 4
            vertices[5] = (VertexLayout) {{ 1, -1,  1}, {1, 0}}; // Vertex 5
            vertices[6] = (VertexLayout) {{ 1,  1,  1}, {1, 1}}; // Vertex 6
            vertices[7] = (VertexLayout) {{-1,  1,  1}, {0, 1}}; // Vertex 7


            SDL_UnmapGPUTransferBuffer(app_state.gpu, vertex_transfer_buffer);
        }

        SDL_GPUTransferBufferCreateInfo index_transfer_buffer_descriptor = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = sizeof(VertexLayout) * 36
        };

        SDL_GPUTransferBuffer *index_transfer_buffer = SDL_CreateGPUTransferBuffer(app_state.gpu, &index_transfer_buffer_descriptor);
        if (!index_transfer_buffer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create transfer buffer. %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }

        {
            Uint32 *indices = SDL_MapGPUTransferBuffer(app_state.gpu, index_transfer_buffer, false);
            if (!indices) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map transfer buffer. %s", SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(app_state.gpu, index_transfer_buffer);
                return SDL_APP_FAILURE;
            }

            // Face 1 (front)
            indices[0]  = 2; indices[1]  = 1; indices[2]  = 0;
            indices[3]  = 0; indices[4]  = 3; indices[5]  = 2;

            // // Face 2 (back)
            indices[6]  = 4;  indices[7] = 5;  indices[8] = 6;
            indices[9]  = 6; indices[10] = 7; indices[11] = 4;

            //  // Face 3 (bottom)
            indices[12] = 0; indices[13] = 1; indices[14] = 5;
            indices[15] = 5; indices[16] = 4; indices[17] = 0;

            //  // Face 4 (top)
            indices[18] = 2; indices[19] = 3; indices[20] = 7;
            indices[21] = 7; indices[22] = 6; indices[23] = 2;

            //  // Face 5 (left)
            indices[24] = 7; indices[25] = 3; indices[26] = 0;
            indices[27] = 0; indices[28] = 4; indices[29] = 7;

            //  // Face 6 (right)
            indices[30] = 6; indices[31] = 5; indices[32] = 1;
            indices[33] = 1; indices[34] = 2; indices[35] = 6;

            SDL_UnmapGPUTransferBuffer(app_state.gpu, index_transfer_buffer);
        }

        SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(app_state.gpu);
        if (!command_buffer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to acquire command buffer. %s", SDL_GetError());
            SDL_ReleaseGPUTransferBuffer(app_state.gpu, vertex_transfer_buffer);
            SDL_ReleaseGPUTransferBuffer(app_state.gpu, index_transfer_buffer);
            return SDL_APP_FAILURE;
        }

        SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(command_buffer);

        SDL_GPUTransferBufferLocation source = {
            .transfer_buffer = vertex_transfer_buffer,
        };

        SDL_GPUBufferRegion destination = {
            .buffer = app_state.vertex_buffer,
            .size = sizeof(VertexLayout) * 8,
        };

        SDL_UploadToGPUBuffer(pass, &source, &destination, false);

        source = (SDL_GPUTransferBufferLocation) {
            .transfer_buffer = index_transfer_buffer,
        };

        destination = (SDL_GPUBufferRegion) {
            .buffer = app_state.index_buffer,
            .size = sizeof(Uint32) * 36,
        };

        SDL_UploadToGPUBuffer(pass, &source, &destination, false);

        SDL_EndGPUCopyPass(pass);

        bool submitted_command_buffer = SDL_SubmitGPUCommandBuffer(command_buffer);

        SDL_ReleaseGPUTransferBuffer(app_state.gpu, vertex_transfer_buffer);
        SDL_ReleaseGPUTransferBuffer(app_state.gpu, index_transfer_buffer);

        if (!submitted_command_buffer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to submit command buffer. %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
    }

    bool created_mesh_pipeline = create_mesh_pipeline();
    if (!created_mesh_pipeline) {
        return SDL_APP_FAILURE;
    }

    bool created_grid_pipeline = create_grid_pipeline();
    if (!created_grid_pipeline) {
        return SDL_APP_FAILURE;
    }

    bool window_shown = SDL_ShowWindow(app_state.window);
    if (!window_shown) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to show main window. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    app_state.nanoseconds_since_init = SDL_GetTicksNS();
    app_state.is_valid = true;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    Uint64 nanoseconds_since_init = SDL_GetTicksNS();
    Uint64 delta_nanoseconds = nanoseconds_since_init - app_state.nanoseconds_since_init;
    app_state.nanoseconds_since_init = nanoseconds_since_init;

    float time = app_state.nanoseconds_since_init / (double) SDL_NS_PER_SECOND;
    float dt = (float) (delta_nanoseconds / (double) SDL_NS_PER_SECOND);

    int width, height;
    if (!SDL_GetWindowSize(app_state.window, &width, &height)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to get window size. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    float aspect_ratio = width / (float) height;

    float phase = time * (HMM_PI * 2.0f) * 0.1f;

    HMM_Mat4 view_matrix = HMM_LookAt_RH(HMM_V3(/*HMM_CosF(phase) * 5.0f*/0, HMM_SinF(phase) * 5.0f, /*HMM_SinF(phase) * 5.0f*/-5), HMM_V3(0, 0, 0), HMM_V3(0, 1, 0));
    HMM_Mat4 projection_matrix = HMM_Perspective_RH_NO(90.0f * HMM_DegToRad, aspect_ratio, 0.3f, 10000.0f);
    app_state.common_uniforms.view_projection_matrix = HMM_MulM4(projection_matrix, view_matrix);
    app_state.vertex_uniforms.inv_view_projection_matrix = HMM_InvGeneralM4(app_state.common_uniforms.view_projection_matrix);

    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(app_state.gpu);
    if (!command_buffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to acquire command buffer. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_GPUTexture *swapchain_texture = NULL;
    Uint32 swapchain_width  = 0;
    Uint32 swapchain_height = 0;

    bool acquired_swapchain_texture = SDL_AcquireGPUSwapchainTexture(command_buffer, app_state.window, &swapchain_texture, &swapchain_width, &swapchain_height);
    if (!acquired_swapchain_texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to acquire swapchain texture. %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    int instance_count = 1;
    app_state.common_uniforms.instance_count = instance_count;
    app_state.common_uniforms.time = app_state.nanoseconds_since_init / (double) SDL_NS_PER_SECOND;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &app_state.common_uniforms, sizeof(CommonUniformBlock));
    SDL_PushGPUVertexUniformData(command_buffer, 1, &app_state.vertex_uniforms, sizeof(VertexUniformBlock));

    SDL_PushGPUFragmentUniformData(command_buffer, 0, &app_state.common_uniforms, sizeof(CommonUniformBlock));
    SDL_PushGPUFragmentUniformData(command_buffer, 1, &app_state.fragment_uniforms, sizeof(FragmentUniformBlock));
    //SDL_PushGPUFragmentUniformData(command_buffer, 2, &app_state.per_instance_fragment_uniforms, sizeof(PerInstanceFragmentUniformBlock));

    SDL_GPUColorTargetInfo clear_target_info = {
        .texture = swapchain_texture,
        .clear_color = { 0.2f, 0.2f, 0.25f, 1.0f },
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };

    SDL_GPUDepthStencilTargetInfo depth_target_info = {
        .texture = app_state.depth_texture,
        .clear_depth = 0.0f,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command_buffer, &clear_target_info, 1, &depth_target_info);
    if (pass) {
        SDL_BindGPUGraphicsPipeline(pass, app_state.grid_pipeline);
        app_state.per_instance_vertex_uniforms.model_matrix = HMM_M4D(1);
        SDL_PushGPUVertexUniformData(command_buffer, 2, &app_state.per_instance_vertex_uniforms, sizeof(PerInstanceVertexUniformBlock));
        SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);

        SDL_BindGPUGraphicsPipeline(pass, app_state.mesh_pipeline);
        SDL_BindGPUVertexBuffers(pass, 0, (SDL_GPUBufferBinding[]) {{.buffer = app_state.vertex_buffer}}, 1);
        SDL_BindGPUIndexBuffer(pass, &(SDL_GPUBufferBinding) {.buffer = app_state.index_buffer}, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        app_state.per_instance_vertex_uniforms.model_matrix = HMM_Translate(HMM_V3(0, HMM_SinF(phase * 2.0f) * 5.0, 0));
        SDL_PushGPUVertexUniformData(command_buffer, 2, &app_state.per_instance_vertex_uniforms, sizeof(PerInstanceVertexUniformBlock));
        SDL_DrawGPUIndexedPrimitives(pass, 36, instance_count, 0, 0, 0);
        
        SDL_EndGPURenderPass(pass);
    }

    SDL_SubmitGPUCommandBuffer(command_buffer);

    return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    if (!app_state.is_valid) {
        return SDL_APP_FAILURE;
    }

    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_Q) {
            return SDL_APP_SUCCESS;
        }
    }

    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        return SDL_APP_SUCCESS;
    }

    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        recreate_depth_texture();
        if (!app_state.depth_texture) {
            return SDL_APP_FAILURE;
        }
    }

    return SDL_APP_CONTINUE;
}
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    if (app_state.mesh_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(app_state.gpu, app_state.mesh_pipeline);
    }

    if (app_state.grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(app_state.gpu, app_state.grid_pipeline);
    }

    if (app_state.depth_texture) {
        SDL_ReleaseGPUTexture(app_state.gpu, app_state.depth_texture);
    }

    if (app_state.vertex_buffer) {
        SDL_ReleaseGPUBuffer(app_state.gpu, app_state.vertex_buffer);
    }

    if (app_state.index_buffer) {
        SDL_ReleaseGPUBuffer(app_state.gpu, app_state.index_buffer);
    }

    if (app_state.window) {
        SDL_ReleaseWindowFromGPUDevice(app_state.gpu, app_state.window);
        SDL_DestroyWindow(app_state.window);
    }

    if (app_state.gpu) {
        SDL_DestroyGPUDevice(app_state.gpu);
    }

    SDL_Quit();
}
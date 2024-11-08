// dear imgui: Renderer Backend for SDL_Renderer for SDL3
// (Requires: SDL 3.0.0+)

// (**IMPORTANT: SDL 3.0.0 is NOT YET RELEASED AND CURRENTLY HAS A FAST CHANGING API. THIS CODE BREAKS OFTEN AS SDL3 CHANGES.**)

// Note how SDL_Renderer is an _optional_ component of SDL3.
// For a multi-platform app consider using e.g. SDL+DirectX on Windows and SDL+OpenGL on Linux/OSX.
// If your application will want to render any non trivial amount of graphics other than UI,
// please be aware that SDL_Renderer currently offers a limited graphic API to the end-user and
// it might be difficult to step out of those boundaries.

// Implemented features:
//  [X] Renderer: User texture binding. Use 'SDL_Texture*' as ImTextureID. Read the FAQ about ImTextureID!
//  [X] Renderer: Large meshes support (64k+ vertices) with 16-bit indices.
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(cImGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.

// You can copy and use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// CHANGELOG
//  2024-10-09: Expose selected render state in ImGui_ImplSDLGpu3_RenderState, which you can access in 'void* platform_io.Renderer_RenderState' during draw callbacks.
//  2024-07-01: Update for SDL3 api changes: SDL_RenderGeometryRaw() uint32 version was removed (SDL#9009).
//  2024-05-14: *BREAKING CHANGE* cImGui_ImplSDLGpu3_RenderDrawData() requires SDL_Renderer* passed as parameter.
//  2024-02-12: Amend to query SDL_RenderViewportSet() and restore viewport accordingly.
//  2023-05-30: Initial version.

#include "dcimgui.h"
#ifndef CIMGUI_DISABLE
#include "custom_imgui_impl_sdlgpu3.h"
#include <stdint.h>     // intptr_t

// Clang warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"    // warning: implicit conversion changes signedness
#endif

// SDL
#include <SDL3/SDL.h>
#if !SDL_VERSION_ATLEAST(3,0,0)
#error This backend requires SDL 3.0.0+
#endif

// SDL_Renderer data
typedef struct ImGui_ImplSDLGpu3_Data_t
{
    SDL_GPUDevice*          GpuDevice;       // Main viewport's renderer
    SDL_GPUTexture*         FontTexture;
    //ImVector<SDL_FColor>    ColorBuffer;

    // cImGui_ImplSDLGpu3_Data()   { memset((void*)this, 0, sizeof(*this)); }
} ImGui_ImplSDLGpu3_Data;

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
static ImGui_ImplSDLGpu3_Data* ImGui_ImplSDLGpu3_GetBackendData()
{
    return ImGui_GetCurrentContext() ? (ImGui_ImplSDLGpu3_Data*)(ImGui_GetIO()->BackendRendererUserData) : NULL;
}

// Functions
bool cImGui_ImplSDLGpu3_Init(SDL_GPUDevice* gpu_device)
{
    ImGuiIO* io = ImGui_GetIO();
    CIMGUI_CHECKVERSION();
    IM_ASSERT(io->BackendRendererUserData == NULL && "Already initialized a renderer backend!");
    IM_ASSERT(gpu_device != NULL && "SDL_Renderer not initialized!");

    // Setup backend capabilities flags
    ImGui_ImplSDLGpu3_Data* bd = CIM_ALLOC(sizeof(ImGui_ImplSDLGpu3_Data));
    *bd = (ImGui_ImplSDLGpu3_Data) { 0 };

    io->BackendRendererUserData = (void *) bd;
    io->BackendRendererName = "imgui_impl_sdlgpu3";
    // io->BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.

    bd->GpuDevice = gpu_device;

    return true;
}

void cImGui_ImplSDLGpu3_Shutdown()
{
    ImGui_ImplSDLGpu3_Data* bd = ImGui_ImplSDLGpu3_GetBackendData();
    IM_ASSERT(bd != NULL && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO* io = ImGui_GetIO();

    cImGui_ImplSDLGpu3_DestroyDeviceObjects();

    io->BackendRendererName = NULL;
    io->BackendRendererUserData = NULL;
    // io->BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    CIM_FREE(bd);
}

void cImGui_ImplSDLGpu3_NewFrame()
{
    ImGui_ImplSDLGpu3_Data* bd = ImGui_ImplSDLGpu3_GetBackendData();
    IM_ASSERT(bd != NULL && "Context or backend not initialized! Did you call cImGui_ImplSDLGpu3_Init()?");

    if (!bd->FontTexture)
        cImGui_ImplSDLGpu3_CreateDeviceObjects();
}

void cImGui_ImplSDLGpu3_RenderDrawData(ImDrawData* draw_data, SDL_GPUDevice* gpu_device)
{
    // cImGui_ImplSDLGpu3_Data* bd = ImGui_ImplSDLGpu3_GetBackendData();

	// // If there's a scale factor set by the user, use that instead
    // // If the user has specified a scale factor to SDL_Renderer already via SDL_RenderSetScale(), SDL will scale whatever we pass
    // // to SDL_RenderGeometryRaw() by that scale factor. In that case we don't want to be also scaling it ourselves here.
    // float rsx = 1.0f;
	// float rsy = 1.0f;
	// SDL_GetRenderScale(renderer, &rsx, &rsy);
    // ImVec2 render_scale;
	// render_scale.x = (rsx == 1.0f) ? draw_data->FramebufferScale.x : 1.0f;
	// render_scale.y = (rsy == 1.0f) ? draw_data->FramebufferScale.y : 1.0f;

	// // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
	// int fb_width = (int)(draw_data->DisplaySize.x * render_scale.x);
	// int fb_height = (int)(draw_data->DisplaySize.y * render_scale.y);
	// if (fb_width == 0 || fb_height == 0)
	// 	return;

    // // Backup SDL_Renderer state that will be modified to restore it afterwards
    // struct BackupSDLRendererState
    // {
    //     SDL_Rect    Viewport;
    //     bool        ViewportEnabled;
    //     bool        ClipEnabled;
    //     SDL_Rect    ClipRect;
    // };
    // BackupSDLRendererState old = {};
    // old.ViewportEnabled = SDL_RenderViewportSet(renderer);
    // old.ClipEnabled = SDL_RenderClipEnabled(renderer);
    // SDL_GetRenderViewport(renderer, &old.Viewport);
    // SDL_GetRenderClipRect(renderer, &old.ClipRect);

    // // Setup desired state
    // cImGui_ImplSDLGpu3_SetupRenderState(renderer);

    // // Setup render state structure (for callbacks and custom texture bindings)
    // ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    // ImGui_ImplSDLGpu3_RenderState render_state;
    // render_state.Renderer = renderer;
    // platform_io.Renderer_RenderState = &render_state;

	// // Will project scissor/clipping rectangles into framebuffer space
	// ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
	// ImVec2 clip_scale = render_scale;

    // // Render command lists
    // for (int n = 0; n < draw_data->CmdListsCount; n++)
    // {
    //     const ImDrawList* draw_list = draw_data->CmdLists[n];
    //     const ImDrawVert* vtx_buffer = draw_list->VtxBuffer.Data;
    //     const ImDrawIdx* idx_buffer = draw_list->IdxBuffer.Data;

    //     for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
    //     {
    //         const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
    //         if (pcmd->UserCallback)
    //         {
    //             // User callback, registered via ImDrawList::AddCallback()
    //             // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
    //             if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
    //                 cImGui_ImplSDLGpu3_SetupRenderState(renderer);
    //             else
    //                 pcmd->UserCallback(draw_list, pcmd);
    //         }
    //         else
    //         {
    //             // Project scissor/clipping rectangles into framebuffer space
    //             ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
    //             ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
    //             if (clip_min.x < 0.0f) { clip_min.x = 0.0f; }
    //             if (clip_min.y < 0.0f) { clip_min.y = 0.0f; }
    //             if (clip_max.x > (float)fb_width) { clip_max.x = (float)fb_width; }
    //             if (clip_max.y > (float)fb_height) { clip_max.y = (float)fb_height; }
    //             if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
    //                 continue;

    //             SDL_Rect r = { (int)(clip_min.x), (int)(clip_min.y), (int)(clip_max.x - clip_min.x), (int)(clip_max.y - clip_min.y) };
    //             SDL_SetRenderClipRect(renderer, &r);

    //             const float* xy = (const float*)(const void*)((const char*)(vtx_buffer + pcmd->VtxOffset) + offsetof(ImDrawVert, pos));
    //             const float* uv = (const float*)(const void*)((const char*)(vtx_buffer + pcmd->VtxOffset) + offsetof(ImDrawVert, uv));
    //             const SDL_Color* color = (const SDL_Color*)(const void*)((const char*)(vtx_buffer + pcmd->VtxOffset) + offsetof(ImDrawVert, col)); // SDL 2.0.19+

    //             // Bind texture, Draw
	// 			SDL_Texture* tex = (SDL_Texture*)pcmd->GetTexID();
    //             SDL_RenderGeometryRaw8BitColor(renderer, bd->ColorBuffer, tex,
    //                 xy, (int)sizeof(ImDrawVert),
    //                 color, (int)sizeof(ImDrawVert),
    //                 uv, (int)sizeof(ImDrawVert),
    //                 draw_list->VtxBuffer.Size - pcmd->VtxOffset,
    //                 idx_buffer + pcmd->IdxOffset, pcmd->ElemCount, sizeof(ImDrawIdx));
    //         }
    //     }
    // }
    // platform_io.Renderer_RenderState = NULL;

    // // Restore modified SDL_Renderer state
    // SDL_SetRenderViewport(renderer, old.ViewportEnabled ? &old.Viewport : nullptr);
    // SDL_SetRenderClipRect(renderer, old.ClipEnabled ? &old.ClipRect : nullptr);
}

// Called by Init/NewFrame/Shutdown
bool cImGui_ImplSDLGpu3_CreateFontsTexture()
{
    ImGuiIO* io = ImGui_GetIO();
    ImGui_ImplSDLGpu3_Data* bd = ImGui_ImplSDLGpu3_GetBackendData();

    // Build texture atlas
    unsigned char* pixels;
    int width, height;
    ImFontAtlas_GetTexDataAsRGBA32(io->Fonts, &pixels, &width, &height, /* out_bytes_per_pixel = */ NULL); // Load as RGBA 32-bit (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.

    bd->FontTexture = SDL_CreateGPUTexture(bd->GpuDevice, &(SDL_GPUTextureCreateInfo){
        .format                 = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .width                  = width,
        .height                 = height,
        .usage                  = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .layer_count_or_depth   = 1,
        .sample_count           = SDL_GPU_SAMPLECOUNT_1,
        .num_levels             = 1,
    });

    if (bd->FontTexture == NULL)
    {
        SDL_Log("error creating texture");
        return false;
    }

    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(bd->GpuDevice, &(SDL_GPUTransferBufferCreateInfo) {
        .size = width * height * 4,
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
    });

    if (!tb)
    {
        SDL_Log("error creating transfer buffer");
        return false;
    }

    void *buffer = SDL_MapGPUTransferBuffer(bd->GpuDevice, tb, false);
    if (!buffer)
    {
        SDL_Log("error creating transfer buffer");
        SDL_ReleaseGPUTransferBuffer(bd->GpuDevice, tb);
        return false;
    }

    SDL_memcpy(buffer, pixels, width * height * 4);

    SDL_UnmapGPUTransferBuffer(bd->GpuDevice, tb);

    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(bd->GpuDevice);
    if (!cb)
    {
        SDL_Log("error acquiring command buffer");
        SDL_ReleaseGPUTransferBuffer(bd->GpuDevice, tb);
        return false;
    }

    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cb);
    if (!cb)
    {
        SDL_Log("error beginning copy pass");
        SDL_CancelGPUCommandBuffer(cb);
        SDL_ReleaseGPUTransferBuffer(bd->GpuDevice, tb);
        return false;
    }

    SDL_UploadToGPUTexture(pass, &(SDL_GPUTextureTransferInfo) {
        .pixels_per_row = width,
        .rows_per_layer = height,
        .offset = 0,
        .transfer_buffer = tb,
    }, &(SDL_GPUTextureRegion) {
        .texture = bd->FontTexture,
        .w = width,
        .h = height,
        .d = 1,
    }, false);

    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(cb);
    SDL_ReleaseGPUTransferBuffer(bd->GpuDevice, tb);

    // Store our identifier
    ImFontAtlas_SetTexID(io->Fonts, (ImTextureID)(intptr_t)bd->FontTexture);

    return true;
}

void cImGui_ImplSDLGpu3_DestroyFontsTexture()
{
    ImGuiIO* io = ImGui_GetIO();
    ImGui_ImplSDLGpu3_Data* bd = ImGui_ImplSDLGpu3_GetBackendData();
    if (bd->FontTexture)
    {
        ImFontAtlas_SetTexID(io->Fonts, 0);
        SDL_ReleaseGPUTexture(bd->GpuDevice, bd->FontTexture);
        bd->FontTexture = NULL;
    }
}

bool cImGui_ImplSDLGpu3_CreateDeviceObjects()
{
    return cImGui_ImplSDLGpu3_CreateFontsTexture();
}

void cImGui_ImplSDLGpu3_DestroyDeviceObjects()
{
    cImGui_ImplSDLGpu3_DestroyFontsTexture();
}

//-----------------------------------------------------------------------------

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef CIMGUI_DISABLE

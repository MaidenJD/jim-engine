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
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(ImGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.

// You can copy and use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#pragma once
#include "dcimgui.h"      // IMGUI_IMPL_API
#ifndef CIMGUI_DISABLE

typedef struct SDL_GPUDevice SDL_GPUDevice;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
bool     cImGui_ImplSDLGpu3_Init(SDL_GPUDevice* gpu_device);
void     cImGui_ImplSDLGpu3_Shutdown();
void     cImGui_ImplSDLGpu3_NewFrame();
void     cImGui_ImplSDLGpu3_RenderDrawData(ImDrawData* draw_data, SDL_GPUDevice* gpu_device);

// Called by Init/NewFrame/Shutdown
bool     cImGui_ImplSDLGpu3_CreateFontsTexture();
void     cImGui_ImplSDLGpu3_DestroyFontsTexture();
bool     cImGui_ImplSDLGpu3_CreateDeviceObjects();
void     cImGui_ImplSDLGpu3_DestroyDeviceObjects();

// [BETA] Selected render state data shared with callbacks.
// This is temporarily stored in GetPlatformIO().Renderer_RenderState during the ImGui_ImplSDLGpu3_RenderDrawData() call.
// (Please open an issue if you feel you need access to more data)
struct ImGui_ImplSDLGpu3_RenderState_t
{
    SDL_GPUDevice*       GpuDevice;
} ImGui_ImplSDLGpu3_RenderState;

#endif // #ifndef CIMGUI_DISABLE

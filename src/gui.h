#pragma once
#include <d3d11.h>
#include "imgui/imgui.h"
#include <windows.h>

extern HWND application_window;
extern ID3D11Device* graphics_device;
extern ID3D11DeviceContext* graphics_context;

namespace window
{
    inline ImVec2 size = ImVec2(640, 400);
    inline float rounding = 6.0f;
}

namespace ui
{
    void initialize();
    void render();
    void shutdown();

    inline ImFont* font_subtitle = nullptr;
    inline ImFont* font_input = nullptr;
    inline ImFont* font_label = nullptr;
    inline ImFont* font_title = nullptr;
}

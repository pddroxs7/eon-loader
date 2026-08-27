#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <tchar.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#include "gui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

ID3D11Device* graphics_device = nullptr;
ID3D11DeviceContext* graphics_context = nullptr;
IDXGISwapChain* swap_chain = nullptr;
ID3D11RenderTargetView* back_buffer_view = nullptr;
HWND application_window = nullptr;

static bool is_dragging_window = false;
static POINT drag_last_cursor = {};

static void render_application_frame(bool low_latency_present = false);
static bool create_graphics_device(HWND window_handle);
static void release_graphics_device();
static void create_back_buffer_view();
static void release_back_buffer_view();
static bool can_start_window_drag();
static void apply_rounded_window_shape(HWND window_handle);
static LRESULT WINAPI window_message_handler(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam);

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    WNDCLASSEX window_class{};
    window_class.cbSize = sizeof(WNDCLASSEX);
    window_class.style = CS_CLASSDC;
    window_class.lpfnWndProc = window_message_handler;
    window_class.hInstance = instance;
    window_class.lpszClassName = _T("EonWindow");

    RegisterClassEx(&window_class);

    application_window = CreateWindow(
        window_class.lpszClassName,
        _T("Eon"),
        WS_POPUP,
        100, 100,
        (int)window::size.x,
        (int)window::size.y,
        nullptr, nullptr, instance, nullptr);

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(application_window, &margins);
    apply_rounded_window_shape(application_window);

    if (!create_graphics_device(application_window))
    {
        release_graphics_device();
        UnregisterClass(window_class.lpszClassName, instance);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui_ImplWin32_Init(application_window);
    ImGui_ImplDX11_Init(graphics_device, graphics_context);

    ui::initialize();

    ImGuiStyle& style = ImGui::GetStyle();
    style.AntiAliasedLines = false;
    style.AntiAliasedFill = false;
    style.WindowRounding = window::rounding;
    style.FrameRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.ScrollbarRounding = 5.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.AntiAliasedLinesUseTex = false;
    style.Alpha = 1.0f;

    ShowWindow(application_window, SW_SHOWDEFAULT);
    UpdateWindow(application_window);

    bool running = true;
    while (running)
    {
        MSG message;
        while (PeekMessage(&message, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
            if (message.message == WM_QUIT)
                running = false;
        }
        if (!running)
            break;

        render_application_frame(false);
    }

    ui::shutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    release_graphics_device();
    DestroyWindow(application_window);
    UnregisterClass(window_class.lpszClassName, instance);

    return 0;
}

static void render_application_frame(bool low_latency_present)
{
    if (!swap_chain || !back_buffer_view || ImGui::GetCurrentContext() == nullptr)
        return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ui::render();

    ImGui::Render();

    const float clear_color[4] = { 0.f, 0.f, 0.f, 0.f };
    graphics_context->OMSetRenderTargets(1, &back_buffer_view, nullptr);
    graphics_context->ClearRenderTargetView(back_buffer_view, clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swap_chain->Present(low_latency_present ? 0 : 1, 0);
}

static bool create_graphics_device(HWND window_handle)
{
    DXGI_SWAP_CHAIN_DESC swap_chain_description{};
    swap_chain_description.BufferCount = 2;
    swap_chain_description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_description.OutputWindow = window_handle;
    swap_chain_description.SampleDesc.Count = 1;
    swap_chain_description.Windowed = TRUE;
    swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_level;
    if (D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION,
        &swap_chain_description, &swap_chain, &graphics_device,
        &feature_level, &graphics_context) != S_OK)
        return false;

    create_back_buffer_view();
    return true;
}

static void create_back_buffer_view()
{
    ID3D11Texture2D* back_buffer = nullptr;
    swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    graphics_device->CreateRenderTargetView(back_buffer, nullptr, &back_buffer_view);
    back_buffer->Release();
}

static void release_back_buffer_view()
{
    if (back_buffer_view)
    {
        back_buffer_view->Release();
        back_buffer_view = nullptr;
    }
}

static void release_graphics_device()
{
    release_back_buffer_view();
    if (swap_chain) { swap_chain->Release(); swap_chain = nullptr; }
    if (graphics_context) { graphics_context->Release(); graphics_context = nullptr; }
    if (graphics_device) { graphics_device->Release(); graphics_device = nullptr; }
}

static bool can_start_window_drag()
{
    if (ImGui::GetCurrentContext() != nullptr && ImGui::IsAnyItemHovered())
        return false;

    return true;
}

static void apply_rounded_window_shape(HWND window_handle)
{
    const int width = (int)window::size.x;
    const int height = (int)window::size.y;
    const int corner_diameter = (int)(window::rounding * 2.0f);

    HRGN window_region = CreateRoundRectRgn(0, 0, width + 1, height + 1, corner_diameter, corner_diameter);
    SetWindowRgn(window_handle, window_region, TRUE);
}

static LRESULT WINAPI window_message_handler(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(window_handle, message, wparam, lparam))
        return true;

    switch (message)
    {
    case WM_NCHITTEST:
        return HTCLIENT;

    case WM_LBUTTONDOWN:
        if (!can_start_window_drag())
            break;

        is_dragging_window = true;
        GetCursorPos(&drag_last_cursor);
        SetCapture(window_handle);
        return 0;

    case WM_MOUSEMOVE:
    {
        POINT cursor;
        GetCursorPos(&cursor);

        if (is_dragging_window && (wparam & MK_LBUTTON))
        {
            RECT window_rect;
            GetWindowRect(window_handle, &window_rect);
            SetWindowPos(
                window_handle,
                NULL,
                window_rect.left + (cursor.x - drag_last_cursor.x),
                window_rect.top + (cursor.y - drag_last_cursor.y),
                0,
                0,
                SWP_NOSIZE | SWP_NOZORDER);
            drag_last_cursor = cursor;
            render_application_frame(true);
        }

        return 0;
    }

    case WM_LBUTTONUP:
        is_dragging_window = false;
        if (GetCapture() == window_handle)
        {
            ReleaseCapture();
            return 0;
        }
        break;

    case WM_ENTERSIZEMOVE:
        SetTimer(window_handle, 1, USER_TIMER_MINIMUM, NULL);
        return 0;

    case WM_EXITSIZEMOVE:
        KillTimer(window_handle, 1);
        return 0;

    case WM_TIMER:
        if (wparam == 1)
            SendMessage(window_handle, WM_PAINT, 0, 0);
        return 0;

    case WM_PAINT:
        if (swap_chain && back_buffer_view && ImGui::GetCurrentContext() != nullptr)
        {
            render_application_frame(true);
            ValidateRect(window_handle, NULL);
        }
        else
        {
            PAINTSTRUCT paint_info;
            BeginPaint(window_handle, &paint_info);
            EndPaint(window_handle, &paint_info);
        }
        return 0;

    case WM_SIZE:
        if (graphics_device != nullptr && wparam != SIZE_MINIMIZED)
        {
            release_back_buffer_view();
            swap_chain->ResizeBuffers(0, (UINT)LOWORD(lparam), (UINT)HIWORD(lparam), DXGI_FORMAT_UNKNOWN, 0);
            create_back_buffer_view();
            apply_rounded_window_shape(window_handle);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(window_handle, message, wparam, lparam);
}

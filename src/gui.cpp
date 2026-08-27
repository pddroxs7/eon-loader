#include "gui.h"
#include "Images/glow.h"
#include "Images/player.h"
#include "Images/key.h"
#include "Images/login.h"
#include "Fonts/fonts.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_internal.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace ui
{
    static ImTextureID g_glow_texture = (ImTextureID)0;
    static ImVec2 g_glow_size = ImVec2(0.0f, 0.0f);

    static ImTextureID g_player_texture = (ImTextureID)0;
    static ImVec2 g_player_size = ImVec2(0.0f, 0.0f);

    static ImTextureID g_key_texture = (ImTextureID)0;
    static ImVec2 g_key_size = ImVec2(0.0f, 0.0f);

    static ImTextureID g_login_texture = (ImTextureID)0;
    static ImVec2 g_login_size = ImVec2(0.0f, 0.0f);

    static ImTextureID CreateTextureFromPixels(unsigned char* pixels, int width, int height)
    {
        if (!graphics_device || !pixels || width < 1 || height < 1)
            return (ImTextureID)0;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = (UINT)width;
        desc.Height = (UINT)height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub{};
        sub.pSysMem = pixels;
        sub.SysMemPitch = (UINT)width * 4;

        ID3D11Texture2D* texture = nullptr;
        if (FAILED(graphics_device->CreateTexture2D(&desc, &sub, &texture)))
            return (ImTextureID)0;

        ID3D11ShaderResourceView* srv = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;

        if (FAILED(graphics_device->CreateShaderResourceView(texture, &srv_desc, &srv)))
        {
            texture->Release();
            return (ImTextureID)0;
        }

        texture->Release();
        return (ImTextureID)srv;
    }

    static bool LoadPngTexture(const unsigned char* data, int data_len, ImTextureID& out_texture, ImVec2& out_size)
    {
        int width = 0;
        int height = 0;
        int channels = 0;

        unsigned char* pixels = stbi_load_from_memory(data, data_len, &width, &height, &channels, 4);
        if (!pixels)
            return false;

        out_texture = CreateTextureFromPixels(pixels, width, height);
        out_size = ImVec2((float)width, (float)height);
        stbi_image_free(pixels);
        return out_texture != (ImTextureID)0;
    }

    static void ReleaseTexture(ImTextureID& texture)
    {
        if (texture)
        {
            ((ID3D11ShaderResourceView*)texture)->Release();
            texture = (ImTextureID)0;
        }
    }

    static void DrawImageHeight(ImDrawList* dl, ImTextureID tex, ImVec2 tex_size, ImVec2 pos, float height, ImU32 tint = IM_COL32(255, 255, 255, 255))
    {
        if (!tex || tex_size.y <= 0.0f)
            return;

        const float scale = height / tex_size.y;
        const ImVec2 draw_size(tex_size.x * scale, height);
        dl->AddImage(tex, pos, ImVec2(pos.x + draw_size.x, pos.y + draw_size.y), ImVec2(0, 0), ImVec2(1, 1), tint);
    }

    static ImU32 LerpU32(ImU32 a, ImU32 b, float t)
    {
        ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
        ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            ca.x + (cb.x - ca.x) * t,
            ca.y + (cb.y - ca.y) * t,
            ca.z + (cb.z - ca.z) * t,
            ca.w + (cb.w - ca.w) * t));
    }

    static float SmoothToward(float current, float target, float speed)
    {
        const float dt = ImGui::GetIO().DeltaTime;
        if (current < target)
            return (current + speed * dt < target) ? current + speed * dt : target;
        if (current > target)
            return (current - speed * dt > target) ? current - speed * dt : target;
        return target;
    }

    static void DrawCenteredText(ImDrawList* dl, ImFont* font, float center_x, float y, ImU32 col, const char* text)
    {
        const ImVec2 size = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.0f, text);
        dl->AddText(font, font->LegacySize, ImVec2(center_x - size.x * 0.5f, y), col, text);
    }

    static void DrawLoginPanel(ImDrawList* dl, ImVec2 wp)
    {
        constexpr float panel_left = 40.0f;
        constexpr float panel_width = 248.0f;
        constexpr float field_height = 44.0f;
        constexpr float field_corner_radius = 6.0f;
        constexpr float row_spacing = 16.0f;
        constexpr float remember_row_height = 20.0f;
        constexpr float button_height = 42.0f;
        constexpr float button_corner_radius = 6.0f;
        constexpr float key_icon_height = 18.0f;
        constexpr float login_icon_height = 17.0f;
        constexpr float header_lift = 22.0f;

        static char license_key[128] = {};
        static bool remember_me = true;
        static float license_field_hover = 0.0f;
        static float license_field_focus = 0.0f;
        static float login_button_hover = 0.0f;
        static float login_button_press = 0.0f;
        static float remember_checkbox_hover = 0.0f;
        static float remember_checkbox_fill = 1.0f;

        ImFont* title_face = font_title ? font_title : ImGui::GetFont();
        ImFont* subtitle_face = font_subtitle ? font_subtitle : ImGui::GetFont();
        ImFont* input_face = font_input ? font_input : ImGui::GetFont();
        ImFont* label_face = font_label ? font_label : ImGui::GetFont();

        const float panel_x = wp.x + panel_left;
        const float panel_center_x = panel_x + panel_width * 0.5f;
        const float base_y = wp.y;

        DrawCenteredText(
            dl,
            title_face,
            panel_center_x,
            base_y + 106.0f - header_lift,
            IM_COL32(255, 255, 255, 255),
            "Eon");

        DrawCenteredText(
            dl,
            subtitle_face,
            panel_center_x,
            base_y + 142.0f - header_lift,
            IM_COL32(118, 118, 118, 255),
            "Pure Competitive Advantages");

        float y = base_y + 168.0f - header_lift;

        const ImVec2 field_min(panel_x, y);
        const ImVec2 field_max(panel_x + panel_width, y + field_height);
        const bool field_hovered = ImGui::IsMouseHoveringRect(field_min, field_max);

        license_field_hover = SmoothToward(license_field_hover, field_hovered ? 1.0f : 0.0f, 10.0f);

        ImU32 field_bg = LerpU32(IM_COL32(18, 18, 18, 255), IM_COL32(26, 26, 26, 255), license_field_hover);
        field_bg = LerpU32(field_bg, IM_COL32(30, 30, 30, 255), license_field_focus);
        dl->AddRectFilled(field_min, field_max, field_bg, field_corner_radius);

        if (g_key_texture)
        {
            const float key_bright = 1.0f + 0.12f * license_field_hover + 0.18f * license_field_focus;
            int key_channel = (int)(140.0f * key_bright);
            if (key_channel > 255)
                key_channel = 255;
            const ImU32 key_tint = IM_COL32(key_channel, key_channel, key_channel, 255);
            const ImVec2 icon_pos(field_min.x + 14.0f, field_min.y + (field_height - key_icon_height) * 0.5f);
            DrawImageHeight(dl, g_key_texture, g_key_size, icon_pos, key_icon_height, key_tint);
        }

        ImGui::SetCursorScreenPos(ImVec2(field_min.x + 40.0f, field_min.y + (field_height - 14.0f) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(
            0.55f + 0.12f * license_field_focus,
            0.55f + 0.12f * license_field_focus,
            0.55f + 0.12f * license_field_focus,
            1.0f));
        ImGui::PushItemWidth(panel_width - 52.0f);
        ImGui::PushFont(input_face);
        ImGui::InputTextWithHint("##license", "License Key", license_key, sizeof(license_key));
        license_field_focus = SmoothToward(license_field_focus, ImGui::IsItemActive() ? 1.0f : 0.0f, 12.0f);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        ImGui::PopFont();
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        y += field_height + row_spacing;

        const char* remember_text = "Remember me";
        const ImVec2 remember_pos(panel_x, y + 1.0f);
        const ImVec2 remember_size = label_face->CalcTextSizeA(label_face->LegacySize, FLT_MAX, 0.0f, remember_text);

        const float checkbox_size = 16.0f;
        const ImVec2 checkbox_min(remember_pos.x + remember_size.x + 8.0f, y + (remember_row_height - checkbox_size) * 0.5f);
        const ImVec2 checkbox_max(checkbox_min.x + checkbox_size, checkbox_min.y + checkbox_size);
        const ImVec2 remember_row_min(panel_x, y);
        const ImVec2 remember_row_max(checkbox_max.x, y + remember_row_height);

        ImGui::SetCursorScreenPos(remember_row_min);
        ImGui::InvisibleButton("##remember", ImVec2(remember_row_max.x - remember_row_min.x, remember_row_height));
        const bool remember_hovered = ImGui::IsItemHovered();
        if (remember_hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked())
            remember_me = !remember_me;

        remember_checkbox_hover = SmoothToward(remember_checkbox_hover, remember_hovered ? 1.0f : 0.0f, 12.0f);
        remember_checkbox_fill = SmoothToward(remember_checkbox_fill, remember_me ? 1.0f : 0.0f, 9.0f);

        const ImU32 remember_col = LerpU32(IM_COL32(106, 106, 106, 255), IM_COL32(170, 170, 170, 255), remember_checkbox_hover);
        dl->AddText(label_face, label_face->LegacySize, remember_pos, remember_col, remember_text);

        const ImU32 checkbox_off_color = LerpU32(IM_COL32(12, 12, 12, 255), IM_COL32(28, 28, 28, 255), remember_checkbox_hover);
        const ImU32 checkbox_on_color = LerpU32(IM_COL32(255, 255, 255, 255), IM_COL32(235, 235, 235, 255), remember_checkbox_hover);
        const ImU32 checkbox_bg = LerpU32(checkbox_off_color, checkbox_on_color, remember_checkbox_fill);
        dl->AddRectFilled(checkbox_min, checkbox_max, checkbox_bg, 1.5f);

        if (remember_checkbox_fill > 0.01f)
        {
            const int check_strength = (int)(255.0f * remember_checkbox_fill);
            ImGui::RenderCheckMark(
                dl,
                ImVec2(checkbox_min.x + 3.0f, checkbox_min.y + 3.0f),
                IM_COL32(0, 0, 0, check_strength),
                checkbox_size - 6.0f);
        }

        y += remember_row_height + 18.0f;

        const ImVec2 button_min(panel_x, y);
        const ImVec2 button_max(panel_x + panel_width, y + button_height);

        ImGui::SetCursorScreenPos(button_min);
        ImGui::InvisibleButton("##login", ImVec2(panel_width, button_height));
        const bool login_hovered = ImGui::IsItemHovered();
        const bool login_pressed = ImGui::IsItemActive();
        if (login_hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        login_button_hover = SmoothToward(login_button_hover, login_hovered ? 1.0f : 0.0f, 11.0f);
        login_button_press = SmoothToward(login_button_press, login_pressed ? 1.0f : 0.0f, 16.0f);

        ImU32 button_bg = LerpU32(IM_COL32(255, 255, 255, 255), IM_COL32(238, 238, 238, 255), login_button_hover);
        button_bg = LerpU32(button_bg, IM_COL32(218, 218, 218, 255), login_button_press);
        dl->AddRectFilled(button_min, button_max, button_bg, button_corner_radius);

        const char* login_label = "Login";
        const ImVec2 text_size = label_face->CalcTextSizeA(label_face->LegacySize, FLT_MAX, 0.0f, login_label);

        float login_icon_width = 0.0f;
        if (g_login_texture && g_login_size.y > 0.0f)
            login_icon_width = g_login_size.x * (login_icon_height / g_login_size.y);

        const float content_width = text_size.x + 10.0f + login_icon_width;
        const float content_x = button_min.x + (panel_width - content_width) * 0.5f;
        const float content_y = button_min.y + (button_height - text_size.y) * 0.5f;

        const ImU32 login_text_color = LerpU32(IM_COL32(0, 0, 0, 255), IM_COL32(40, 40, 40, 255), login_button_press);
        dl->AddText(label_face, label_face->LegacySize, ImVec2(content_x, content_y), login_text_color, login_label);

        if (g_login_texture)
        {
            constexpr float login_icon_slide = 5.0f;
            const float icon_x = content_x + text_size.x + 10.0f + login_icon_slide * login_button_hover;
            const float icon_y = button_min.y + (button_height - login_icon_height) * 0.5f;
            DrawImageHeight(dl, g_login_texture, g_login_size, ImVec2(icon_x, icon_y), login_icon_height, login_text_color);
        }
    }

    void initialize()
    {
        ImGuiIO& io = ImGui::GetIO();

        ImFontConfig form_font_settings;
        form_font_settings.OversampleH = 3;
        form_font_settings.OversampleV = 2;
        form_font_settings.PixelSnapH = true;
        form_font_settings.RasterizerMultiply = 1.22f;

        ImFontConfig title_font_settings;
        title_font_settings.OversampleH = 4;
        title_font_settings.OversampleV = 3;
        title_font_settings.PixelSnapH = true;
        title_font_settings.RasterizerMultiply = 1.32f;
        title_font_settings.GlyphExtraAdvanceX = 1.4f;

        ImFontConfig subtitle_font_settings;
        subtitle_font_settings.OversampleH = 3;
        subtitle_font_settings.OversampleV = 2;
        subtitle_font_settings.PixelSnapH = true;
        subtitle_font_settings.RasterizerMultiply = 1.2f;
        subtitle_font_settings.GlyphExtraAdvanceX = 0.45f;

        if (!font_subtitle)
            font_subtitle = io.Fonts->AddFontFromMemoryTTF((void*)Fonts::montserrat_bold, Fonts::montserrat_bold_len, 13.0f, &subtitle_font_settings);
        if (!font_input)
            font_input = io.Fonts->AddFontFromMemoryTTF((void*)Fonts::montserrat_bold, Fonts::montserrat_bold_len, 14.5f, &form_font_settings);
        if (!font_label)
            font_label = io.Fonts->AddFontFromMemoryTTF((void*)Fonts::montserrat_bold, Fonts::montserrat_bold_len, 15.5f, &form_font_settings);
        if (!font_title)
            font_title = io.Fonts->AddFontFromMemoryTTF((void*)Fonts::montserrat_bold, Fonts::montserrat_bold_len, 35.0f, &title_font_settings);

        ImGui_ImplDX11_InvalidateDeviceObjects();
        ImGui_ImplDX11_CreateDeviceObjects();

        LoadPngTexture(glow_rawData, (int)glow_png_len, g_glow_texture, g_glow_size);
        LoadPngTexture(player_rawData, (int)player_png_len, g_player_texture, g_player_size);
        LoadPngTexture(key_rawData, (int)key_png_len, g_key_texture, g_key_size);
        LoadPngTexture(login_rawData, (int)login_png_len, g_login_texture, g_login_size);
    }

    void render()
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(window::size);
        ImGui::Begin("##main", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 br(wp.x + window::size.x, wp.y + window::size.y);

        const ImColor bg(1, 1, 1);
        dl->AddRectFilled(wp, br, bg);

        dl->PushClipRect(wp, br, true);

        if (g_glow_texture && g_glow_size.x > 0.0f && g_glow_size.y > 0.0f)
        {
            constexpr float glow_width_scale = 0.72f;
            constexpr float glow_top_crop = 0.42f;

            const float scale = (window::size.x / g_glow_size.x) * glow_width_scale;
            const ImVec2 full_size(g_glow_size.x * scale, g_glow_size.y * scale);
            const float visible_height = full_size.y * (1.0f - glow_top_crop);
            const ImVec2 pos(
                wp.x + (window::size.x - full_size.x) * 0.5f,
                wp.y);
            const ImVec2 end(pos.x + full_size.x, pos.y + visible_height);

            dl->AddImage(
                g_glow_texture,
                pos,
                end,
                ImVec2(0.0f, glow_top_crop),
                ImVec2(1.0f, 1.0f));
        }

        if (g_player_texture && g_player_size.x > 0.0f && g_player_size.y > 0.0f)
        {
            constexpr float player_height_scale = 0.82f;
            constexpr float fade_distance = 0.58f;

            const float scale = (window::size.y / g_player_size.y) * player_height_scale;
            const ImVec2 draw_size(g_player_size.x * scale, g_player_size.y * scale);
            const ImVec2 pos(br.x - draw_size.x, br.y - draw_size.y);
            const ImVec2 end(br.x, br.y);

            dl->AddImage(g_player_texture, pos, end, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));

            const ImU32 fade_top = IM_COL32(1, 1, 1, 0);
            const ImU32 fade_bottom = IM_COL32(1, 1, 1, 255);
            const ImVec2 fade_start(pos.x, pos.y + draw_size.y * fade_distance);
            dl->AddRectFilledMultiColor(fade_start, end, fade_top, fade_top, fade_bottom, fade_bottom);
        }

        DrawLoginPanel(dl, wp);

        dl->PopClipRect();

        ImGui::End();
    }

    void shutdown()
    {
        ReleaseTexture(g_glow_texture);
        ReleaseTexture(g_player_texture);
        ReleaseTexture(g_key_texture);
        ReleaseTexture(g_login_texture);
        g_glow_size = ImVec2(0.0f, 0.0f);
        g_player_size = ImVec2(0.0f, 0.0f);
        g_key_size = ImVec2(0.0f, 0.0f);
        g_login_size = ImVec2(0.0f, 0.0f);
    }
}

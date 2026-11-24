#pragma once

#include "imgui.h"
#include <algorithm>

inline float ui_scale_for_window(float dpi, int width, int height, int percent)
{
    // Auto keeps the normal DPI size where there is room. Explicit sizes
    // have a looser cap so the menu and dialogs stay reachable on small windows.
    const float preferred = percent ? dpi * percent / 100.0f : dpi;
    const float fit = std::min(width / (percent ? 480.0f : 960.0f),
                               height / (percent ? 300.0f : 540.0f));
    return std::max(percent ? 0.5f : 0.625f, std::min(preferred, fit));
}

inline void apply_imgui_theme(float scale = 1.0f)
{
    auto &style = ImGui::GetStyle();
    const float font_size = style.FontSizeBase;
    style = ImGuiStyle{};
    ImGui::StyleColorsDark(&style);

    style.TabRounding = 4.0f;
    style.ScrollbarRounding = 9.0f;
    style.WindowRounding = 7.0f;
    style.GrabRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.ScrollbarSize = 10.0f;

    // ImGui defaults to borderless frames, including checkboxes and radio buttons.
    style.FrameBorderSize = 1.0f;
    style.Colors[ImGuiCol_Border] = ImVec4(0.48f, 0.50f, 0.55f, 1.00f);
    style.Colors[ImGuiCol_CheckboxSelectedBg] = ImVec4(0.16f, 0.24f, 0.34f, 1.00f);

    style.ScaleAllSizes(scale);
    style.FontSizeBase = font_size;
    style.FontScaleMain = scale;
    // ScaleAllSizes truncates border widths; keep outlines visible below 100%.
    style.FrameBorderSize = std::max(1.0f, style.FrameBorderSize);
    style.WindowBorderSize = std::max(1.0f, style.WindowBorderSize);
    style.ChildBorderSize = std::max(1.0f, style.ChildBorderSize);
    style.PopupBorderSize = std::max(1.0f, style.PopupBorderSize);
}

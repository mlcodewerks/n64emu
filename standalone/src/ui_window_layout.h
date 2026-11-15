#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <string>

// Keep layout state with the ImGui window, so hidden windows also catch up when
// reopened and rebuilding an ImGui context cannot leave stale window pointers.
inline ImRect ui_work_area() {
    const auto *view = ImGui::GetMainViewport();
    const float padding = std::min(8.f, std::max(0.f, std::min(view->WorkSize.x, view->WorkSize.y) * 0.05f));
    return ImRect(ImVec2(view->WorkPos.x + padding, view->WorkPos.y + padding),
        ImVec2(view->WorkPos.x + std::max(padding + 1.f, view->WorkSize.x - padding),
               view->WorkPos.y + std::max(padding + 1.f, view->WorkSize.y - padding)));
}

inline void ui_prepare_window(ImGuiWindow *window, ImVec2 fraction, bool centered = false, ImVec2 forced_size = {}) {
    const auto area = ui_work_area();
    const auto available = area.GetSize();
    ImVec2 size(available.x * fraction.x, available.y * fraction.y);
    ImVec2 position(area.Min.x + (available.x - size.x) * 0.5f, area.Min.y + (available.y - size.y) * 0.5f);
    if (window) {
        size = window->SizeFull; position = window->Pos;
        auto &state = window->StateStorage;
        const ImGuiID key = ImHashStr("n64emu viewport layout", 0, window->ID);
        const ImVec2 previous_size(state.GetFloat(key, available.x), state.GetFloat(key + 1, available.y));
        const ImVec2 previous_origin(state.GetFloat(key + 2, area.Min.x), state.GetFloat(key + 3, area.Min.y));
        if (previous_size.x > 0 && previous_size.y > 0) {
            const ImVec2 scale(available.x / previous_size.x, available.y / previous_size.y);
            size = ImVec2(size.x * scale.x, size.y * scale.y);
            position = ImVec2(area.Min.x + (position.x - previous_origin.x) * scale.x,
                             area.Min.y + (position.y - previous_origin.y) * scale.y);
        }
        state.SetFloat(key, available.x); state.SetFloat(key + 1, available.y);
        state.SetFloat(key + 2, area.Min.x); state.SetFloat(key + 3, area.Min.y);
    }
    if (forced_size.x > 0 && forced_size.y > 0) size = forced_size;
    const auto minimum = ImGui::GetStyle().WindowMinSize;
    size.x = std::clamp(size.x, std::min(minimum.x, available.x), available.x);
    size.y = std::clamp(size.y, std::min(minimum.y, available.y), available.y);
    if (centered) position = ImVec2(area.Min.x + (available.x - size.x) * 0.5f, area.Min.y + (available.y - size.y) * 0.5f);
    position.x = std::clamp(position.x, area.Min.x, area.Max.x - size.x);
    position.y = std::clamp(position.y, area.Min.y, area.Max.y - size.y);
    // A host such as Shader Studio may supply an explicit panel position.
    if (!(GImGui->NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasPos)) ImGui::SetNextWindowPos(position);
    if (!window || size.x != window->SizeFull.x || size.y != window->SizeFull.y)
        ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(minimum.x, available.x), std::min(minimum.y, available.y)), available);
}

inline void ui_prepare_window(const char *name, ImVec2 fraction, bool centered = false, ImVec2 forced_size = {}) {
    ui_prepare_window(ImGui::FindWindowByName(name), fraction, centered, forced_size);
}


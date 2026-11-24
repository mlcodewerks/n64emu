#include "ui_fonts.h"
#include "imgui_theme.h"
#include "imgui.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>

int main() {
    try {
        ImGui::CreateContext();
        init_ui_fonts();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1920, 1080);
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        if (!io.FontDefault || io.Fonts->Fonts.Size != 1)
            throw std::runtime_error("Merged font atlas failed to initialize");

        for (float scale : {1.0f, 0.625f, 1.5f, 2.0f, 1.0f}) {
            apply_imgui_theme(scale);
            ImGui::NewFrame();
            if (ImGui::GetFontSize() != 16.0f * scale)
                throw std::runtime_error("Text did not follow live UI scaling");
            auto *baked = io.FontDefault->GetFontBaked(ImGui::GetFontSize());
            for (ImWchar codepoint : {ImWchar('A'), ImWchar('0'), ImWchar(0x00e9), ImWchar(0xf013),
                                     ImWchar(0xf04b), ImWchar(0xf07c)}) {
                auto *glyph = baked->FindGlyphNoFallback(codepoint);
                if (!glyph || !glyph->Visible)
                    throw std::runtime_error("Roboto text or Fork Awesome icon is missing");
            }
            ImGui::EndFrame();
        }
        if (ui_scale_for_window(2, 1920, 1080, 0) != 2 ||
            std::abs(ui_scale_for_window(1.5f, 800, 600, 0) - 800.0f / 960) > 0.00001f ||
            ui_scale_for_window(1, 1600, 900, 50) != 0.5f ||
            ui_scale_for_window(1, 1600, 900, 125) != 1.25f ||
            ui_scale_for_window(2, 720, 480, 200) != 1.5f)
            throw std::runtime_error("UI scaling did not fit the client area");
        ImGui::DestroyContext();
        std::puts("PASS embedded Roboto/Fork Awesome glyphs and live UI scaling");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

#include "input_window.h"
#include "imgui_theme.h"
#include "dsp_chain_window.h"
#include "ui_window_layout.h"
#include <cstdio>
#include <stdexcept>

static void contained(const char *name) {
    const auto *window = ImGui::FindWindowByName(name);
    if (!window || window->Hidden)
        throw std::runtime_error("Configuration dialog did not render");
    const auto area = ui_work_area();
    if (window->Pos.x < area.Min.x - 1 || window->Pos.y < area.Min.y - 1 ||
        window->Pos.x + window->Size.x > area.Max.x + 1 ||
        window->Pos.y + window->Size.y > area.Max.y + 1)
        throw std::runtime_error("Configuration dialog escaped the viewport");
}

int main() {
    try {
        ImGui::CreateContext();
        apply_imgui_theme();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DeltaTime = 1.f / 60;
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Input input;
        dsp_chain chain;
        dsp_chain_window dsp;
        dsp.visible = true;
        bool open = true;
        for (const auto size : {ImVec2(1280, 960), ImVec2(640, 480), ImVec2(320, 240), ImVec2(1920, 1080)}) {
            io.DisplaySize = size;
            for (int frame = 0; frame < 3; ++frame) {
                ImGui::NewFrame();
                draw_input_settings(input, open);
                dsp.draw(chain, ".");
                ImGui::Render();
            }
            contained("Input Settings");
            contained("Audio DSP");
            const auto *window = ImGui::FindWindowByName("Input Settings");
            if (std::abs(window->Size.x - size.x * 0.7f) > 1 ||
                window->Size.y < size.y * 0.3f - 1 || window->Size.y > size.y * 0.5f + 1)
                throw std::runtime_error("Input popup dimensions differ from wtweg");
            const auto *tabs = GImGui->TabBars.GetByKey(ImHashStr("MyTabBar", 0, window->ID));
            if (input.player_count() != 4 || !tabs || tabs->Tabs.Size != 4)
                throw std::runtime_error("Input settings must expose exactly four player tabs");
        }
        input.begin_capture(0, {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A});
        open = false;
        ImGui::NewFrame();
        draw_input_settings(input, open);
        ImGui::Render();
        if (input.capturing())
            throw std::runtime_error("Closing input settings left capture active");
        ImGui::DestroyContext();
        std::puts("PASS configuration dialogs, viewport resizing and capture cleanup");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

#include "input_window.h"
#include "imgui_theme.h"
#include "dsp_chain_window.h"
#include "shader_chain_window.h"
#include "ui_window_layout.h"
#include <cstdio>
#include <stdexcept>

static void contained(const char *name) {
    const auto *window = ImGui::FindWindowByName(name);
    if (!window || window->Hidden)
        throw std::runtime_error("Settings did not render");
    const auto area = ui_work_area();
    if (window->Pos.x < area.Min.x - 1 || window->Pos.y < area.Min.y - 1 ||
        window->Pos.x + window->Size.x > area.Max.x + 1 ||
        window->Pos.y + window->Size.y > area.Max.y + 1)
        throw std::runtime_error("Settings escaped the viewport");
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
        const char *tabs[] = {"Video", "Audio", "Input"};
        for (const auto size : {ImVec2(1280, 960), ImVec2(640, 480), ImVec2(320, 240), ImVec2(1920, 1080)}) {
            io.DisplaySize = size;
            for (int selected = 0; selected < 3; ++selected) {
                ImGuiID player_tabs = 0;
                for (int frame = 0; frame < 3; ++frame) {
                    ImGui::NewFrame();
                    ui_prepare_window("Settings", ImVec2(0.8f, 0.85f));
                    if (ImGui::Begin("Settings") && ImGui::BeginTabBar("settings")) {
                        for (int tab = 0; tab < 3; ++tab) {
                            if (!ImGui::BeginTabItem(tabs[tab], nullptr, tab == selected ? ImGuiTabItemFlags_SetSelected : 0))
                                continue;
                            if (tab == 2) {
                                player_tabs = ImGui::GetID("Players");
                                draw_input_settings(input);
                            } else {
                                if (ImGui::BeginChild(tabs[tab], {}, 0, ImGuiWindowFlags_HorizontalScrollbar)) {
                                    if (tab == 0)
                                        draw_shader_chain_settings();
                                    else
                                        dsp.draw_settings(chain, ".");
                                }
                                ImGui::EndChild();
                            }
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                    ImGui::End();
                    dsp.draw_picker(selected == 1);
                    draw_shader_chain_dialogs(selected == 0);
                    ImGui::Render();
                }
                contained("Settings");
                if (selected == 2) {
                    const auto *players = GImGui->TabBars.GetByKey(player_tabs);
                    if (input.player_count() != 4 || !players || players->Tabs.Size != 4)
                        throw std::runtime_error("Input settings must expose exactly four player tabs");
                }
            }
        }
        for (const char *name : {"Input Settings", "Audio DSP", "Shader Chains"})
            if (ImGui::FindWindowByName(name))
                throw std::runtime_error("Settings opened a separate editor window");
        ImGui::DestroyContext();
        std::puts("PASS embedded settings, viewport resizing and four player tabs");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

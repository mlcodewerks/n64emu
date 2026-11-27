#include "config_store.h"
#include "core_options_window.h"
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

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Expected an isolated settings directory");
        const auto directory = fs::u8path(argv[1]);
        fs::create_directories(directory);
        Json values(cJSON_CreateObject(), cJSON_Delete);
        cJSON_AddStringToObject(values.get(), "n64emu_cycle_timing", "off");
        config_write_section(directory, "core_options", values.get());
        ImGui::CreateContext();
        apply_imgui_theme();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DeltaTime = 1.f / 60;
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Input input;
        Video video;
        Core core(video, input, directory);
        auto timing = std::find_if(core.options.begin(), core.options.end(),
            [](const Option &option) { return option.key == "n64emu_cycle_timing"; });
        if (core.loaded() || timing == core.options.end() || timing->value != "off")
            throw std::runtime_error("Core options must restore before a ROM is opened");
        core.set_option(*timing, "on");
        core.unload();
        values = config_section(directory, "core_options");
        const auto *saved = cJSON_GetObjectItemCaseSensitive(values.get(), "n64emu_cycle_timing");
        if (!cJSON_IsString(saved) || std::string(saved->valuestring) != "on")
            throw std::runtime_error("Core options must save before a ROM is opened");
        dsp_chain chain;
        dsp_chain_window dsp;
        const char *tabs[] = {"Video", "Audio", "Input", "Core"};
        for (const auto size : {ImVec2(1280, 960), ImVec2(640, 480), ImVec2(320, 240), ImVec2(1920, 1080)}) {
            io.DisplaySize = size;
            for (size_t selected = 0; selected < std::size(tabs); ++selected) {
                ImGuiID player_tabs = 0, core_table = 0;
                for (int frame = 0; frame < 3; ++frame) {
                    ImGui::NewFrame();
                    ui_prepare_window("Settings", ImVec2(0.8f, 0.85f));
                    if (ImGui::Begin("Settings") && ImGui::BeginTabBar("settings")) {
                        for (size_t tab = 0; tab < std::size(tabs); ++tab) {
                            if (!ImGui::BeginTabItem(tabs[tab], nullptr, tab == selected ? ImGuiTabItemFlags_SetSelected : 0))
                                continue;
                            if (tab == 2) {
                                player_tabs = ImGui::GetID("Players");
                                draw_input_settings(input);
                            } else {
                                if (ImGui::BeginChild(tabs[tab], {}, 0, ImGuiWindowFlags_HorizontalScrollbar)) {
                                    if (tab == 0)
                                        draw_shader_chain_settings();
                                    else if (tab == 1)
                                        dsp.draw_settings(chain, ".");
                                    else {
                                        core_table = ImGui::GetID("Options");
                                        draw_core_settings(core);
                                    }
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
                if (selected == 3 && (!core_table || !GImGui->Tables.GetByKey(core_table)))
                    throw std::runtime_error("Core tab must expose options before a ROM is opened");
            }
        }
        for (const char *name : {"Input Settings", "Audio DSP", "Shader Chains", "Core Settings"})
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

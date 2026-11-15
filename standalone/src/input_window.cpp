#include "input_window.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdio>

namespace {
std::string description(Input &input, const std::vector<InputSource> &sources) {
    return sources.empty() ? "None" : input.binding_name(sources);
}

void draw_binding(Input &input, unsigned player, unsigned index, InputOutput output) {
    auto &bind = input.binding(player, output);
    if (bind.label.empty())
        return;
    ImGui::TextWrapped("%s", bind.label.c_str());
    ImGui::PushID(int(player));
    ImGui::PushID(int(index));
    const float available = ImGui::GetContentRegionAvail().x;
    const auto draw_capture = [&] {
        ImGui::SameLine(available * 0.80f);
        ImGui::SetNextItemWidth(available * 0.20f);
        char buffer[128]{};
        const auto name = description(input, bind.positive);
        std::snprintf(buffer, sizeof(buffer), "%s", name.c_str());
        ImGui::InputText("##joykey", buffer, sizeof(buffer));
        if (ImGui::IsItemActivated() || ImGui::IsItemClicked()) {
            ImGui::SetWindowFocus();
            input.begin_capture(player, output);
        }
    };
    const bool stick_axis = output.device == RETRO_DEVICE_ANALOG && output.index < 2 && output.id < 2;
    if (stick_axis) {
        int source = int(bind.mode);
        if (ImGui::Combo("Source", &source, "Controller / key / mouse button\0Mouse\0Digital directions\0")) {
            input.cancel_capture();
            bind.mode = AnalogMode(source);
            bind.value = 0;
            bind.custom = true;
            if (bind.mode == AnalogMode::Mouse)
                input.assign("mouse", player, true);
        }
        if (bind.mode == AnalogMode::Mapped)
            draw_capture();
        if (output.id == RETRO_DEVICE_ID_ANALOG_Y)
            bind.custom |= ImGui::Checkbox("Invert Y axis", &bind.invert);
        if (bind.mode == AnalogMode::Mouse) {
            bind.custom |= ImGui::SliderFloat("Mouse sensitivity", &bind.mouse_sensitivity, 1.f, 32767.f,
                                             "%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextWrapped("Mouse %s motion drives this axis and returns to center when stopped. Sensitivity is analog units per mouse pixel.", output.id == 0 ? "X" : "Y");
            ImGui::PopID();
            ImGui::PopID();
            return;
        }
        if (bind.mode == AnalogMode::Digital) {
            bind.custom |= ImGui::SliderFloat("Emulated strength", &bind.strength, 0.f, 100.f,
                                             "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
            for (int direction = 0; direction < 2; ++direction) {
                ImGui::PushID(direction);
                const bool negative = direction == 0;
                auto &sources = negative ? bind.negative : bind.positive;
                ImGui::TextUnformatted(output.id == 0 ? (negative ? "Left" : "Right") : (negative ? "Up" : "Down"));
                ImGui::SameLine();
                const auto name = description(input, sources);
                if (ImGui::Button(input.capturing(player, output, negative)
                                      ? "Press a key, mouse button or controller input..." : name.c_str()))
                    input.begin_capture(player, output, negative);
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                    sources.clear();
                    bind.custom = true;
                    input.cancel_capture();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
            ImGui::PopID();
            return;
        }
    }

    if (!stick_axis)
        draw_capture();
    if (input_axis(output)) {
        bind.custom |= ImGui::SliderFloat("Analog sensitivity", &bind.sensitivity, 0.f, 100.f,
                                         "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextWrapped("100%% uses the original input strength. Lower sensitivity reduces the input strength.");
    }
    ImGui::PopID();
    ImGui::PopID();
}
} // namespace

void draw_input_settings(Input &input, bool &open) {
    if (!open) {
        input.cancel_capture();
        return;
    }
    ImGui::OpenPopup("Input Settings");
    const auto &io = ImGui::GetIO();
    ImGui::SetNextWindowSizeConstraints(ImVec2(io.DisplaySize.x * 0.7f, io.DisplaySize.y * 0.3f),
                                        ImVec2(io.DisplaySize.x * 0.7f, io.DisplaySize.y * 0.5f));
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Input Settings", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushItemWidth(ImMin(260.f, ImGui::GetContentRegionAvail().x * 0.4f));
        auto *state = ImGui::GetStateStorage();
        const auto capture_key = ImGui::GetID("Capture active");
        if (state->GetBool(capture_key) && !input.capturing()) {
            ImGui::ClearActiveID();
            ImGui::SetWindowFocus(nullptr);
        }
        if (ImGui::BeginTabBar("MyTabBar", ImGuiTabBarFlags_None)) {
            for (unsigned player = 0; player < input.player_count(); ++player) {
                const auto tab = "Player " + std::to_string(player + 1);
                if (ImGui::BeginTabItem(tab.c_str())) {
                    if (ImGui::IsItemActivated())
                        input.cancel_capture();
                    ImGui::PushID(int(player));
                    int scope = input.uses_game_input(player) ? 1 : 0;
                    ImGui::BeginDisabled(!input.has_game());
                    if (ImGui::Combo("Bindings", &scope, "Core\0This game\0") &&
                        !input.set_game_input(player, scope == 1))
                        input.status = "Could not save input settings. " + input.status;
                    ImGui::EndDisabled();
                    if (input.uses_game_input(player))
                        ImGui::TextWrapped("These bindings apply only to this game and player port.");
                    else
                        ImGui::TextWrapped("These core bindings are shared by games using this controller layout. Selecting \"This game\" starts with a copy of them.");
                    if (!input.has_game())
                        ImGui::TextDisabled("Load a game to use game bindings.");
                    ImGui::PopID();
                    const auto selected = input.profile.players[player].controller;
                    for (const auto &choice : input.controller_choices(player))
                        if (choice.id == selected) {
                            ImGui::Text("Controller: %s", choice.name.c_str());
                            break;
                        }
                    const auto outputs = input.visible_bindings(player);
                    for (unsigned index = 0; index < outputs.size(); ++index)
                        draw_binding(input, player, index, outputs[index]);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        if (ImGui::Button("OK")) {
            input.cancel_capture();
            std::string error;
            if (input.save_player_configuration(error)) {
                input.status.clear();
                open = false;
                ImGui::CloseCurrentPopup();
            } else {
                input.status = "Could not save input settings. Check that the input configuration is writable, then press OK to retry. " + error;
            }
        }
        if (!input.status.empty() && input.status.find("Could not save input settings.") == 0)
            ImGui::TextWrapped("%s", input.status.c_str());
        state->SetBool(capture_key, input.capturing());
        ImGui::PopItemWidth();
        ImGui::EndPopup();
    }
    if (!open)
        input.cancel_capture();
}

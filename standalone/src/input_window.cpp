#include "input_window.h"
#include "imgui.h"

namespace {
void source_editor(Input &input, unsigned player, InputOutput output, bool negative) {
    auto &binding = input.binding(player, output);
    auto &sources = negative ? binding.negative : binding.positive;
    ImGui::PushID(negative ? 1 : 0);
    const bool capturing = input.capturing(player, output, negative);
    auto description = input.binding_name(sources);
    if (ImGui::Button(capturing ? "Press an input (Escape cancels)" : description.c_str(), ImVec2(-1, 0)))
        input.begin_capture(player, output, negative, false);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", description.c_str());
    if (ImGui::SmallButton("Add input"))
        input.begin_capture(player, output, negative, true);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        input.cancel_capture();
        sources.clear();
        binding.custom = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Choose key"))
        ImGui::OpenPopup("Keyboard keys");
    if (ImGui::BeginPopup("Keyboard keys")) {
        ImGui::TextUnformatted("Add a keyboard input");
        ImGui::BeginChild("Key list", ImVec2(230, 260));
        for (unsigned key = 1; key < 256; ++key) {
            auto name = input_key_name(key);
            if (name == "Unknown key")
                continue;
            ImGui::PushID(int(key));
            if (ImGui::Selectable(name.c_str())) {
                input.cancel_capture();
                sources.push_back({InputKind::Key, "keyboard", int(key), 0});
                binding.custom = true;
                input.assign("keyboard", player, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();
}
} // namespace
void draw_input_settings(Input &input) {
    static int player = 0;
    player = std::min(player, int(input.player_count() - 1));
    auto player_name = "Player " + std::to_string(player + 1);
    if (ImGui::BeginCombo("Player", player_name.c_str())) {
        for (unsigned i = 0; i < input.player_count(); ++i)
            if (ImGui::Selectable(("Player " + std::to_string(i + 1)).c_str(), i == unsigned(player))) {
                input.cancel_capture();
                player = int(i);
            }
        ImGui::EndCombo();
    }
    const char *scope_names[] = {"Global defaults", "This core", "This game"};
    if (ImGui::BeginCombo("Profile", scope_names[unsigned(input.scope())])) {
        for (unsigned i = 0; i < 3; ++i) {
            bool available = i == 0 || (i == 1 ? input.has_core() : input.has_game());
            if (ImGui::Selectable(scope_names[i], unsigned(input.scope()) == i,
                                  available ? 0 : ImGuiSelectableFlags_Disabled))
                input.select_scope(InputScope(i));
        }
        ImGui::EndCombo();
    }
    auto &selected = input.profile.players[player].controller;
    const auto &choices = input.controller_choices(player);
    std::string selected_name = "Disconnected";
    for (const auto &choice : choices)
        if (choice.id == selected)
            selected_name = choice.name;
    if (ImGui::BeginCombo("Emulated controller", selected_name.c_str())) {
        for (const auto &choice : choices)
            if (ImGui::Selectable(choice.name.c_str(), choice.id == selected))
                input.set_controller(player, choice.id);
        ImGui::EndCombo();
    }
    if (ImGui::CollapsingHeader("Devices assigned to this player", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("Select any number of controllers. Their buttons combine; the strongest stick "
                           "movement wins. A device may also feed more than one player.");
        for (const auto &device : input.device_list()) {
            ImGui::PushID(device.id.c_str());
            bool enabled = input.assigned(device.id, player);
            auto label = device.name + (device.connected ? "" : " (disconnected)");
            if (ImGui::Checkbox(label.c_str(), &enabled))
                input.assign(device.id, player, enabled);
            ImGui::PopID();
        }
        if (ImGui::Button("Rescan controllers"))
            input.rescan();
    }
    ImGui::TextUnformatted("Click a binding to replace it, or Add input to keep alternate controls.");
    // Copy keys: editing stick modes can add backing bindings to a sparse profile.
    const auto outputs = input.visible_bindings(player);
    if (outputs.empty())
        ImGui::TextWrapped("No input bindings are exposed for this player by the core.");
    const float table_height =
        std::max(120.0f, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 4.5f);
    if (ImGui::BeginTable("Input bindings", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                          ImVec2(0, table_height))) {
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 0.65f);
        ImGui::TableHeadersRow();
        for (const auto &output : outputs) {
            auto &binding = input.binding(player, output);
            ImGui::PushID(int(output.device));
            ImGui::PushID(int(output.index));
            ImGui::PushID(int(output.id));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextWrapped("%s", binding.label.c_str());
            ImGui::TableSetColumnIndex(1);
            if (input_axis(output)) {
                int mode = int(binding.mode);
                if (ImGui::Combo("Source", &mode,
                                 "Controller / key / mouse button\0Mouse motion\0Digital directions\0")) {
                    input.cancel_capture();
                    binding.mode = AnalogMode(mode);
                    binding.custom = true;
                    if (binding.mode == AnalogMode::Digital) {
                        const auto negative = input
                                                  .binding(player, {RETRO_DEVICE_JOYPAD, 0,
                                                                    input_vertical_axis(output) ? 4u : 6u})
                                                  .positive;
                        const auto positive = input
                                                  .binding(player, {RETRO_DEVICE_JOYPAD, 0,
                                                                    input_vertical_axis(output) ? 5u : 7u})
                                                  .positive;
                        auto &updated = input.binding(player, output);
                        updated.negative = negative;
                        updated.positive = positive;
                    }
                }
                auto &axis = input.binding(player, output);
                if (axis.mode == AnalogMode::Mouse) {
                    ImGui::SliderFloat("Mouse sensitivity", &axis.mouse_sensitivity, 1, 32767,
                                       "%.0f units/pixel",
                                       ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
                    ImGui::TextWrapped("Mouse motion returns to center when stopped.");
                } else {
                    if (axis.mode == AnalogMode::Digital) {
                        ImGui::TextUnformatted(input_vertical_axis(output) ? "Up / negative"
                                                                           : "Left / negative");
                        source_editor(input, player, output, true);
                        ImGui::TextUnformatted(input_vertical_axis(output) ? "Down / positive"
                                                                           : "Right / positive");
                        ImGui::SliderFloat("Emulated strength", &axis.strength, 0, 100, "%.0f%%",
                                           ImGuiSliderFlags_AlwaysClamp);
                    } else {
                        ImGui::SliderFloat("Sensitivity", &axis.sensitivity, 0, 400, "%.0f%%",
                                           ImGuiSliderFlags_AlwaysClamp);
                        ImGui::SliderFloat("Deadzone", &axis.deadzone, 0, 95, "%.0f%%",
                                           ImGuiSliderFlags_AlwaysClamp);
                    }
                    source_editor(input, player, output, false);
                }
                ImGui::Checkbox(input_vertical_axis(output) ? "Invert Y axis" : "Invert X axis",
                                &input.binding(player, output).invert);
            } else {
                source_editor(input, player, output, false);
                auto &updated = input.binding(player, output);
                if (std::any_of(updated.positive.begin(), updated.positive.end(),
                                [](const InputSource &s) { return s.kind == InputKind::Axis; }))
                    ImGui::SliderFloat("Axis activation", &updated.threshold, 1, 100, "%.0f%%",
                                       ImGuiSliderFlags_AlwaysClamp);
            }
            ImGui::PopID();
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (input.capturing()) {
        if (ImGui::Button("Cancel capture"))
            input.cancel_capture();
        ImGui::SameLine();
    }
    if (ImGui::Button("Save input profile")) {
        std::string error;
        if (input.save_configuration(error))
            input.status = "Input profile saved.";
        else
            input.status = error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset this player's bindings"))
        input.reset_player(player);
    if (!input.status.empty())
        ImGui::TextWrapped("%s", input.status.c_str());
}

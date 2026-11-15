#include "config_store.h"
#include "input.h"
#include "dsp_chain.h"
#include "shader_chain.h"
#include <fstream>
#include <cstdio>

static void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    try {
        const auto directory = fs::absolute(fs::u8path(argv[1])) / std::to_wstring(GetTickCount64());
        fs::create_directories(directory / L"config");
        std::string error;
        auto legacy = directory / L"config" / L"input.cfg";
        auto profile = default_input_profile();
        profile.players[3].bindings[0].positive = {{InputKind::Key, "keyboard", 'P', 0}};
        profile.device_names["dinput:test"] = u8"Contrôleur 日本語";
        require(save_input_profile(legacy, profile, error), error.c_str());
        dsp_settings dsp;
        dsp.stages.push_back(dsp_default_stage("echo"));
        require(dsp_save(directory / L"winweg.dsp", dsp, error), error.c_str());
        shader_settings shaders;
        shader_chain_config chain;
        chain.name = u8"Test 日本語";
        chain.passes.push_back({directory / L"example.slang", false, {{"filter_linear", "true"}}});
        chain.values["GAIN"] = 0.5f;
        chain.globals["textures"] = "LUT";
        shaders.chains.push_back(chain);
        require(shader_save(directory / L"winweg.shaders", shaders, error), error.c_str());
        Json seed(cJSON_CreateObject(), cJSON_Delete);
        config_write_section(directory, "seed", seed.get());
        config_initialize(directory);
        require(bool(config_section(directory, "frontend")), "Existing JSON did not gain frontend defaults");
        InputProfile migrated;
        require(config_input_load(directory, legacy, migrated, error), error.c_str());
        dsp_settings migrated_dsp;
        require(config_dsp_load(directory, migrated_dsp, error), error.c_str());
        shader_settings migrated_shaders;
        require(config_shader_load(directory, migrated_shaders, error), error.c_str());
        require(migrated.players[3].bindings[0].positive[0].code == 'P' &&
                    migrated.device_names["dinput:test"] == u8"Contrôleur 日本語" &&
                    migrated_dsp.stages[0].filter == "echo" &&
                    migrated_shaders.chains[0].name == chain.name &&
                    migrated_shaders.chains[0].values["GAIN"] == 0.5f &&
                    !migrated_shaders.chains[0].passes[0].enabled,
                "Migration lost input/DSP/shader settings or Unicode");

        Json custom(cJSON_CreateString("preserve me"), cJSON_Delete);
        config_write_section(directory, "extension", custom.get());
        migrated.players[3].bindings[0].positive[0].code = 'R';
        require(config_input_save(directory, legacy, migrated, error), error.c_str());
        InputProfile restored;
        require(config_input_load(directory, legacy, restored, error) &&
                    restored.players[3].bindings[0].positive[0].code == 'R',
                "Old input file overrode the JSON profile");
        migrated_dsp.enabled = false;
        require(config_dsp_save(directory, migrated_dsp, error), error.c_str());
        dsp_settings restored_dsp;
        require(config_dsp_load(directory, restored_dsp, error) && !restored_dsp.enabled,
                "Old DSP preset overrode the JSON settings");
        migrated_shaders.chains[0].values["GAIN"] = 0.75f;
        require(config_shader_save(directory, migrated_shaders, error), error.c_str());
        shader_settings restored_shaders;
        require(config_shader_load(directory, restored_shaders, error) &&
                    restored_shaders.chains[0].values["GAIN"] == 0.75f,
                "Old shader stack overrode the JSON settings");
        auto extension = config_section(directory, "extension");
        require(extension && std::string(extension->valuestring) == "preserve me",
                "Saving one section clobbered an unrelated section");
        auto inputs = config_section(directory, "input");
        auto global = cJSON_GetObjectItemCaseSensitive(inputs.get(), "input.cfg");
        require(global && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(global, "players")) == 4,
                "Input JSON must contain four structured player entries");
        auto players = cJSON_GetObjectItemCaseSensitive(global, "players");
        auto bindings = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(players, 0), "bindings");
        auto unsupported = cJSON_Duplicate(cJSON_GetArrayItem(bindings, 0), true);
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(unsupported, "id"), 14);
        cJSON_AddItemToArray(bindings, unsupported);
        config_write_section(directory, "input", inputs.get());
        require(config_input_load(directory, legacy, restored, error) &&
                    restored.players[0].bindings.size() == 16 &&
                    restored.players[3].bindings[0].positive[0].code == 'R',
                "Removing unsupported bindings lost supported N64 mappings");
        auto normalized = config_section(directory, "input");
        auto normalized_profile = cJSON_GetObjectItemCaseSensitive(normalized.get(), "input.cfg");
        auto normalized_players = cJSON_GetObjectItemCaseSensitive(normalized_profile, "players");
        require(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(
                    cJSON_GetArrayItem(normalized_players, 0), "bindings")) == 16,
                "Unsupported binding remained in n64emu.json");

        const auto fresh = directory / L"fresh";
        Input input;
        input.load_configuration(fresh);
        input.begin_core(fresh / L"n64emu", fresh / L"game.z64");
        require(input.save_player_configuration(error), error.c_str());
        require(fs::exists(fresh / L"n64emu.json") && !fs::exists(fresh / L"config"),
                "Input configuration was written outside n64emu.json");

        // A malformed document must never be silently overwritten by a save.
        const std::string invalid = "{broken JSON";
        write_file(directory / L"n64emu.json", invalid.data(), invalid.size());
        require(!config_dsp_save(directory, dsp, error), "Malformed JSON was overwritten");
        const auto bytes = read_file(directory / L"n64emu.json");
        require(std::string(bytes.begin(), bytes.end()) == invalid, "Failed save changed the document");
        std::puts("PASS unified JSON: migration, four players, Unicode, round trips, section preservation and failed-save safety");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}

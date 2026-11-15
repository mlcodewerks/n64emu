#include "config_store.h"
#include "input_bindings.h"
#include "dsp_chain.h"
#include "shader_chain.h"
#include <cmath>
#include <iomanip>
#include <sstream>

namespace {
Json own(cJSON *value) {
    if (!value) throw std::runtime_error("Cannot allocate configuration JSON");
    return Json(value, cJSON_Delete);
}
const cJSON *get(const cJSON *object, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(object, key);
}
void require(bool condition) {
    if (!condition) throw std::runtime_error("Invalid n64emu.json configuration value");
}
const char *string(const cJSON *value) {
    require(cJSON_IsString(value) && std::strlen(value->valuestring) <= 65536);
    return value->valuestring;
}
double number(const cJSON *value, double minimum, double maximum) {
    require(cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
            value->valuedouble >= minimum && value->valuedouble <= maximum);
    return value->valuedouble;
}
unsigned integer(const cJSON *value, unsigned maximum) {
    double result = number(value, 0, maximum);
    require(std::floor(result) == result);
    return unsigned(result);
}
bool flag(const cJSON *value) {
    require(cJSON_IsBool(value));
    return cJSON_IsTrue(value);
}
void object(const cJSON *value) { require(cJSON_IsObject(value)); }
void array(const cJSON *value, int maximum) {
    require(cJSON_IsArray(value) && cJSON_GetArraySize(value) <= maximum);
}
Json strings(const std::map<std::string, std::string> &values) {
    auto result = own(cJSON_CreateObject());
    for (const auto &entry : values)
        cJSON_AddStringToObject(result.get(), entry.first.c_str(), entry.second.c_str());
    return result;
}
std::map<std::string, std::string> strings(const cJSON *value) {
    object(value);
    std::map<std::string, std::string> result;
    for (auto *entry = value->child; entry; entry = entry->next) {
        require(result.size() < 4096);
        result[entry->string] = string(entry);
    }
    return result;
}
std::string input_key(const fs::path &directory, const fs::path &legacy) {
    return legacy.lexically_relative(directory / L"config").generic_u8string();
}
void put(cJSON *parent, const char *key, Json value) {
    cJSON_DeleteItemFromObjectCaseSensitive(parent, key);
    if (!cJSON_AddItemToObject(parent, key, value.get()))
        throw std::runtime_error("Cannot update configuration JSON");
    value.release();
}
template<class F> bool attempt(std::string &error, F action) {
    try { action(); error.clear(); return true; }
    catch (const std::exception &e) { error = e.what(); return false; }
}
Json input_json(const InputProfile &profile) {
    auto value = own(cJSON_CreateObject());
    cJSON_AddNumberToObject(value.get(), "game_players", profile.game_players);
    put(value.get(), "device_names", strings(profile.device_names));
    auto assignments = cJSON_AddObjectToObject(value.get(), "assignments");
    for (const auto &entry : profile.assignments)
        cJSON_AddNumberToObject(assignments, entry.first.c_str(), entry.second);
    auto players = cJSON_AddArrayToObject(value.get(), "players");
    for (const auto &player : profile.players) {
        auto port = cJSON_CreateObject(); cJSON_AddItemToArray(players, port);
        cJSON_AddNumberToObject(port, "controller", player.controller);
        auto bindings = cJSON_AddArrayToObject(port, "bindings");
        for (const auto &b : player.bindings) {
            if (!n64_input_output(b.output)) continue;
            auto binding = cJSON_CreateObject(); cJSON_AddItemToArray(bindings, binding);
            cJSON_AddNumberToObject(binding, "device", b.output.device);
            cJSON_AddNumberToObject(binding, "index", b.output.index);
            cJSON_AddNumberToObject(binding, "id", b.output.id);
            cJSON_AddNumberToObject(binding, "mode", unsigned(b.mode));
            cJSON_AddNumberToObject(binding, "sensitivity", b.sensitivity);
            cJSON_AddNumberToObject(binding, "deadzone", b.deadzone);
            cJSON_AddNumberToObject(binding, "mouse_sensitivity", b.mouse_sensitivity);
            cJSON_AddNumberToObject(binding, "strength", b.strength);
            cJSON_AddNumberToObject(binding, "threshold", b.threshold);
            cJSON_AddBoolToObject(binding, "invert", b.invert);
            cJSON_AddBoolToObject(binding, "custom", b.custom);
            for (unsigned side = 0; side < 2; ++side) {
                auto sources = cJSON_AddArrayToObject(binding, side ? "negative" : "positive");
                for (const auto &s : side ? b.negative : b.positive) {
                    auto source = cJSON_CreateObject(); cJSON_AddItemToArray(sources, source);
                    cJSON_AddNumberToObject(source, "kind", unsigned(s.kind));
                    cJSON_AddStringToObject(source, "device", s.device.c_str());
                    cJSON_AddNumberToObject(source, "code", s.code);
                    cJSON_AddNumberToObject(source, "direction", s.direction);
                }
            }
        }
    }
    return value;
}
InputProfile input_from_json(const cJSON *value) {
    object(value);
    // Reuse the native profile validator after decoding JSON into its record stream.
    std::ostringstream out;
    out.imbue(std::locale::classic()); out << std::setprecision(9);
    out << "WINWEG_INPUT 2\nGAMEPLAYERS " << integer(get(value, "game_players"), 15) << '\n';
    for (const auto &name : strings(get(value, "device_names")))
        out << "NAME " << std::quoted(name.first) << ' ' << std::quoted(name.second) << '\n';
    auto assignments = get(value, "assignments"); object(assignments);
    for (auto entry = assignments->child; entry; entry = entry->next)
        out << "ASSIGN " << std::quoted(entry->string) << ' ' << integer(entry, 15) << '\n';
    auto players = get(value, "players"); array(players, 4);
    require(cJSON_GetArraySize(players) == 4);
    for (unsigned port = 0; port < 4; ++port) {
        auto player = cJSON_GetArrayItem(players, port); object(player);
        out << "PORT " << port << ' ' << integer(get(player, "controller"), 65535) << '\n';
        auto bindings = get(player, "bindings"); array(bindings, 512);
        for (auto b = bindings->child; b; b = b->next) {
            object(b);
            std::ostringstream output;
            output << integer(get(b, "device"), RETRO_DEVICE_POINTER) << ' '
                   << integer(get(b, "index"), 255) << ' ' << integer(get(b, "id"), 65535);
            out << "BIND " << port << ' ' << output.str() << ' ' << integer(get(b, "mode"), 2);
            for (auto key : {"sensitivity", "deadzone", "mouse_sensitivity", "strength", "threshold"})
                out << ' ' << number(get(b, key), 0, 32767);
            out << ' ' << flag(get(b, "invert")) << ' ' << flag(get(b, "custom")) << '\n';
            for (unsigned side = 0; side < 2; ++side) {
                auto sources = get(b, side ? "negative" : "positive"); array(sources, 32);
                for (auto s = sources->child; s; s = s->next) {
                    object(s);
                    double direction = number(get(s, "direction"), -1, 3);
                    require(std::floor(direction) == direction);
                    out << "SOURCE " << port << ' ' << output.str() << ' ' << side << ' '
                        << integer(get(s, "kind"), unsigned(InputKind::Hat)) << ' '
                        << std::quoted(string(get(s, "device"))) << ' '
                        << integer(get(s, "code"), 65535) << ' ' << direction << '\n';
                }
            }
        }
    }
    InputProfile result; std::string error;
    if (!parse_input_profile(out.str(), result, error)) throw std::runtime_error(error);
    return result;
}
} // namespace

Json config_read(const fs::path &directory) {
    auto path = directory / L"n64emu.json";
    if (!fs::exists(path)) {
        auto root = own(cJSON_CreateObject());
        cJSON_AddNumberToObject(root.get(), "version", 1);
        return root;
    }
    require(fs::file_size(path) <= 16 * 1024 * 1024);
    auto bytes = read_file(path); bytes.push_back(0);
    Json root(cJSON_ParseWithLengthOpts(reinterpret_cast<const char *>(bytes.data()), bytes.size(), nullptr, true), cJSON_Delete);
    object(root.get());
    require(integer(get(root.get(), "version"), 1) == 1);
    return root;
}
Json config_section(const fs::path &directory, const char *section) {
    auto root = config_read(directory);
    auto item = get(root.get(), section);
    return Json(item ? cJSON_Duplicate(item, true) : nullptr, cJSON_Delete);
}
void config_write_section(const fs::path &directory, const char *section, const cJSON *value) {
    auto root = config_read(directory);
    put(root.get(), section, own(cJSON_Duplicate(value, true)));
    std::unique_ptr<char, decltype(&cJSON_free)> text(cJSON_Print(root.get()), cJSON_free);
    if (!text) throw std::runtime_error("Cannot format configuration JSON");
    fs::create_directories(directory);
    write_file(directory / L"n64emu.json", text.get(), std::strlen(text.get()));
}
void config_initialize(const fs::path &directory) {
    auto root = config_read(directory);
    if (!get(root.get(), "frontend")) {
        auto frontend = own(cJSON_CreateObject());
        cJSON_AddBoolToObject(frontend.get(), "integer_scale", false);
        cJSON_AddNumberToObject(frontend.get(), "state_slot", 0);
        config_write_section(directory, "frontend", frontend.get());
    }
}
bool config_input_exists(const fs::path &directory, const fs::path &legacy) {
    auto inputs = config_section(directory, "input");
    return (inputs && get(inputs.get(), input_key(directory, legacy).c_str())) || fs::exists(legacy);
}
bool config_input_load(const fs::path &directory, const fs::path &legacy, InputProfile &profile, std::string &error) {
    return attempt(error, [&] {
        auto inputs = config_section(directory, "input");
        if (auto value = get(inputs.get(), input_key(directory, legacy).c_str())) {
            auto next = input_from_json(value);
            size_t stored_count = 0, supported_count = 0;
            auto players = get(value, "players");
            for (unsigned player = 0; player < input_players; ++player) {
                stored_count += cJSON_GetArraySize(get(cJSON_GetArrayItem(players, player), "bindings"));
                supported_count += next.players[player].bindings.size();
            }
            if (stored_count != supported_count && !config_input_save(directory, legacy, next, error))
                throw std::runtime_error(error);
            profile = std::move(next);
        } else {
            if (!load_input_profile(legacy, profile, error)) throw std::runtime_error(error);
            if (!config_input_save(directory, legacy, profile, error)) throw std::runtime_error(error);
        }
    });
}
bool config_input_save(const fs::path &directory, const fs::path &legacy, const InputProfile &profile, std::string &error) {
    return attempt(error, [&] {
        auto value = input_json(profile);
        input_from_json(value.get()); // Validate before replacing the stored profile.
        auto inputs = config_section(directory, "input");
        if (!inputs) inputs = own(cJSON_CreateObject());
        object(inputs.get());
        put(inputs.get(), input_key(directory, legacy).c_str(), std::move(value));
        config_write_section(directory, "input", inputs.get());
    });
}
bool config_dsp_save(const fs::path &directory, const dsp_settings &settings, std::string &error) {
    return attempt(error, [&] {
        auto value = own(cJSON_CreateObject());
        cJSON_AddBoolToObject(value.get(), "enabled", settings.enabled);
        auto stages = cJSON_AddArrayToObject(value.get(), "stages");
        require(settings.stages.size() <= 32);
        for (const auto &stage : settings.stages) {
            require(dsp_find_filter(stage.filter) != nullptr);
            auto item = cJSON_CreateObject(); cJSON_AddItemToArray(stages, item);
            cJSON_AddStringToObject(item, "filter", stage.filter.c_str());
            cJSON_AddBoolToObject(item, "enabled", stage.enabled);
            put(item, "parameters", strings(stage.parameters));
        }
        config_write_section(directory, "dsp", value.get());
    });
}
bool config_dsp_load(const fs::path &directory, dsp_settings &settings, std::string &error) {
    return attempt(error, [&] {
        auto value = config_section(directory, "dsp");
        if (!value) {
            if (!fs::exists(directory / L"winweg.dsp")) {
                if (!config_dsp_save(directory, settings, error)) throw std::runtime_error(error);
                return;
            }
            if (!dsp_load(directory / L"winweg.dsp", settings, error) ||
                !config_dsp_save(directory, settings, error)) throw std::runtime_error(error);
            return;
        }
        object(value.get()); dsp_settings next;
        next.enabled = flag(get(value.get(), "enabled"));
        auto stages = get(value.get(), "stages"); array(stages, 32);
        for (auto item = stages->child; item; item = item->next) {
            object(item); auto id = string(get(item, "filter"));
            require(dsp_find_filter(id) != nullptr);
            auto stage = dsp_default_stage(id);
            stage.enabled = flag(get(item, "enabled"));
            stage.parameters = strings(get(item, "parameters"));
            next.stages.push_back(std::move(stage));
        }
        settings = std::move(next);
    });
}
bool config_shader_save(const fs::path &directory, const shader_settings &settings, std::string &error) {
    return attempt(error, [&] {
        auto value = own(cJSON_CreateObject());
        cJSON_AddBoolToObject(value.get(), "enabled", settings.enabled);
        auto chains = cJSON_AddArrayToObject(value.get(), "chains");
        require(settings.chains.size() <= 16);
        for (const auto &chain : settings.chains) {
            auto item = cJSON_CreateObject(); cJSON_AddItemToArray(chains, item);
            cJSON_AddStringToObject(item, "name", chain.name.c_str());
            cJSON_AddBoolToObject(item, "enabled", chain.enabled);
            put(item, "globals", strings(chain.globals));
            auto values = cJSON_AddObjectToObject(item, "values");
            for (const auto &entry : chain.values) {
                require(std::isfinite(entry.second));
                cJSON_AddNumberToObject(values, entry.first.c_str(), entry.second);
            }
            auto passes = cJSON_AddArrayToObject(item, "passes");
            require(chain.passes.size() <= 64);
            for (const auto &pass : chain.passes) {
                auto p = cJSON_CreateObject(); cJSON_AddItemToArray(passes, p);
                cJSON_AddStringToObject(p, "source", pass.source.generic_u8string().c_str());
                cJSON_AddBoolToObject(p, "enabled", pass.enabled);
                put(p, "options", strings(pass.options));
            }
        }
        config_write_section(directory, "shaders", value.get());
    });
}
bool config_shader_load(const fs::path &directory, shader_settings &settings, std::string &error) {
    return attempt(error, [&] {
        auto value = config_section(directory, "shaders");
        if (!value) {
            if (!fs::exists(directory / L"winweg.shaders")) {
                if (!config_shader_save(directory, settings, error)) throw std::runtime_error(error);
                return;
            }
            if (!shader_load(directory / L"winweg.shaders", settings, error) ||
                !config_shader_save(directory, settings, error)) throw std::runtime_error(error);
            return;
        }
        object(value.get()); shader_settings next;
        next.enabled = flag(get(value.get(), "enabled"));
        auto chains = get(value.get(), "chains"); array(chains, 16);
        for (auto item = chains->child; item; item = item->next) {
            object(item); shader_chain_config chain;
            chain.name = string(get(item, "name")); chain.enabled = flag(get(item, "enabled"));
            chain.globals = strings(get(item, "globals"));
            auto values = get(item, "values"); object(values);
            for (auto entry = values->child; entry; entry = entry->next)
                chain.values[entry->string] = float(number(entry, -3.4e38, 3.4e38));
            auto passes = get(item, "passes"); array(passes, 64);
            for (auto p = passes->child; p; p = p->next) {
                object(p); shader_pass pass;
                pass.source = fs::u8path(string(get(p, "source")));
                if (pass.source.is_relative()) pass.source = directory / pass.source;
                pass.enabled = flag(get(p, "enabled")); pass.options = strings(get(p, "options"));
                chain.passes.push_back(std::move(pass));
            }
            next.chains.push_back(std::move(chain));
        }
        settings = std::move(next);
    });
}

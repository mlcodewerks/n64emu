#include "config_store.h"
#include "core.h"
#include "shader_chain.h"
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <file/config_file.h>

Core::Core(Video &v, Input &i, const fs::path &dir) : video(v), input(i), directory(dir) {
    for (auto name : {L"system", L"saves"}) fs::create_directories(dir / name);
    config_initialize(dir);
    input.load_configuration(dir);
    // Query the built-in core's option definitions before any ROM is opened.
    const auto system = (directory / L"system").u8string();
    n64emu_host host{};
    host.system_directory = system.c_str();
    if (!n64emu_init(&host)) throw std::runtime_error("Cannot initialize n64emu core");
    try { restore_options(); }
    catch (...) { n64emu_deinit(); throw; }
    n64emu_deinit();
}
Core::~Core() { try { unload(); } catch (...) {} }
void Core::log(const std::string &text, LogSource source, retro_log_level level) { append_log(text, source, level); }
void Core::load(const fs::path &content) {
    unload();
    content_path = fs::absolute(content);
    save_path = directory / L"saves" / L"n64emu";
    fs::create_directories(save_path);
    save_path /= content_path.stem();
    input.begin_core(directory / L"n64emu", content_path);
    static const retro_controller_description pad[] = {{"Nintendo 64 controller", RETRO_DEVICE_JOYPAD}};
    static const retro_controller_info ports[] = {{pad, 1}, {pad, 1}, {pad, 1}, {pad, 1}, {nullptr, 0}};
    input.set_controller_info(ports);
    std::string system = (directory / L"system").u8string();
    n64emu_host host{};
    host.userdata = this;
    host.system_directory = system.c_str();
    host.video = [](void *p, const void *pixels, unsigned w, unsigned h, size_t pitch) {
        auto &self = *static_cast<Core *>(p);
        try { self.video.refresh(pixels, w, h, pitch); }
        catch (const std::exception &e) { self.callback_error = e.what(); }
    };
    host.audio = [](void *p, const int16_t *samples, size_t frames) {
        auto &self = *static_cast<Core *>(p);
        try { if (self.audio_enabled) self.audio.push(samples, frames); }
        catch (const std::exception &e) { self.callback_error = e.what(); }
    };
    host.input = [](void *p, unsigned port) {
        auto &controls = static_cast<Core *>(p)->input;
        n64emu_input result{};
        for (unsigned id = 0; id < 16; ++id)
            if (controls.state(port, RETRO_DEVICE_JOYPAD, 0, id)) result.buttons |= uint16_t(1u << id);
        result.stick_x = controls.state(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        result.stick_y = controls.state(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
        return result;
    };
    host.rumble = [](void *p, unsigned port, bool strong, uint16_t strength) {
        static_cast<Core *>(p)->input.rumble(port, strong ? RETRO_RUMBLE_STRONG : RETRO_RUMBLE_WEAK, strength);
    };
    host.log = [](void *p, unsigned level, const char *message) {
        try { static_cast<Core *>(p)->log(message, LogSource::Core, static_cast<retro_log_level>(std::min(level, 3u))); }
        catch (...) {} // C callbacks never propagate C++ exceptions.
    };
    host.av_changed = [](void *p, const n64emu_av *av) {
        auto &self = *static_cast<Core *>(p);
        try { self.apply_av(*av); }
        catch (const std::exception &e) { self.callback_error = e.what(); }
    };
    try {
        if (!n64emu_init(&host)) throw std::runtime_error("Cannot initialize n64emu core");
        initialized = true;
        restore_options();
        auto bytes = read_file(content_path);
        auto path = content_path.u8string();
        if (!n64emu_load(path.c_str(), bytes.data(), bytes.size())) throw std::runtime_error("n64emu rejected the ROM");
        game_loaded = true;
        video.set_format(RETRO_PIXEL_FORMAT_XRGB8888);
        n64emu_av av{};
        n64emu_get_av(&av);
        apply_av(av);
        video.start_core({av.width, av.height, av.max_width, av.max_height, av.aspect});
        static const char *labels[] = {"A Button", "B Button", "L Shoulder", "Start", "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right", "C-Down", "C-Up", "C-Left", "C-Right", "Z Trigger", "R Shoulder"};
        std::vector<retro_input_descriptor> descriptors;
        for (unsigned port = 0; port < 4; ++port) {
            for (unsigned id = 0; id < 14; ++id) descriptors.push_back({port, RETRO_DEVICE_JOYPAD, 0, id, labels[id]});
            descriptors.push_back({port, RETRO_DEVICE_ANALOG, 0, 0, "Control Stick X"});
            descriptors.push_back({port, RETRO_DEVICE_ANALOG, 0, 1, "Control Stick Y"});
        }
        descriptors.push_back({});
        input.set_descriptors(descriptors.data());
        auto ram = save_path; ram += L".srm";
        if (fs::exists(ram)) {
            auto saved = read_file(ram);
            size_t size = 0; void *data = n64emu_save_memory(&size);
            if (data && size == saved.size()) std::memcpy(data, saved.data(), size);
            else log("Ignoring battery save with incorrect size\n", LogSource::Frontend, RETRO_LOG_WARN);
        }
        if (!callback_error.empty()) throw std::runtime_error(callback_error);
        title = "n64emu - " + content_path.filename().u8string();
        frames = 0; paused = false;
        log("Loaded built-in n64emu core: " + content_path.u8string() + "\n");
    } catch (...) { unload(); throw; }
}
void Core::unload() {
    std::string failure;
    if (game_loaded) {
        try { save_ram(); save_options(); } catch (const std::exception &e) { failure = e.what(); }
    }
    audio.stop();
    video.stop_core();
    if (initialized) n64emu_deinit();
    initialized = game_loaded = false;
    cheat_entries.clear();
    input.end_core();
    callback_error.clear(); title.clear();
    if (!failure.empty()) log("Save error: " + failure + "\n", LogSource::Frontend, RETRO_LOG_ERROR);
}
void Core::apply_av(const n64emu_av &av) {
    if (audio_enabled && (!audio.running() || sample_rate != av.sample_rate)) audio.start(av.sample_rate);
    sample_rate = av.sample_rate; fps = av.fps;
    video_shaders().source_fps = float(fps);
    video.geometry({av.width, av.height, av.max_width, av.max_height, av.aspect}, true);
}
void Core::run() {
    if (!game_loaded || paused) return;
    if (audio.running()) audio.begin_frame();
    for (unsigned port = 0; port < 4; ++port)
        n64emu_set_controller(port, input.profile.players[port].controller != RETRO_DEVICE_NONE);
    n64emu_run();
    if (!callback_error.empty()) throw std::runtime_error(callback_error);
    if (++frames % 600 == 0) save_ram();
}
void Core::reset() { if (game_loaded) { n64emu_reset(); audio.clear(); audio.dsp.reset(float(sample_rate)); } }

std::vector<CheatEntry> read_retroarch_cheats(const fs::path &path) {
    const auto bytes = read_file(path);
    if (std::find(bytes.begin(), bytes.end(), 0) != bytes.end())
        throw std::runtime_error("Expected a RetroArch text cheat file");
    std::string text(bytes.begin(), bytes.end());
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) text.erase(0, 3);
    // Cheat lists use plain assignments. Ignore comment directives so an
    // imported list cannot open additional files through the config reader.
    std::istringstream lines(text);
    std::string assignments, line;
    size_t fields = 0;
    while (std::getline(lines, line)) {
        const auto first = line.find_first_not_of(" \t\r\v\f");
        if (first == std::string::npos || line[first] == '#') continue;
        assignments += line + '\n';
        ++fields;
    }
    std::unique_ptr<config_file_t, decltype(&config_file_free)> file(
        config_file_new_from_string(assignments.data(), nullptr), config_file_free);
    if (!file) throw std::runtime_error("Invalid RetroArch cheat list");
    auto field = [&](const std::string &key) -> std::string {
        char *value = nullptr;
        if (!config_get_string(file.get(), key.c_str(), &value)) return {};
        std::unique_ptr<char, decltype(&std::free)> owned(value, std::free);
        return owned.get();
    };
    const auto count_text = field("cheats");
    const char *count_end = count_text.data() + count_text.size();
    unsigned count = 0;
    const auto parsed = std::from_chars(count_text.data(), count_end, count);
    if (parsed.ec != std::errc{} || parsed.ptr != count_end || count > fields)
        throw std::runtime_error("Invalid RetroArch cheat list");

    std::vector<CheatEntry> imported;
    imported.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        const auto key = "cheat" + std::to_string(i) + '_';
        auto code = field(key + "code"), label = field(key + "desc");
        if (label.empty()) label = "Cheat " + std::to_string(i + 1);
        const auto *handler = config_get_entry(file.get(), (key + "handler").c_str());
        const bool supported = (!handler || std::strcmp(handler->value, "0") == 0) && n64emu_cheat_validate(code.c_str());
        bool enabled = false;
        config_get_bool(file.get(), (key + "enable").c_str(), &enabled);
        imported.push_back({std::move(label), std::move(code), supported && enabled, supported});
    }
    return imported;
}

void Core::load_cheats(const fs::path &path) {
    if (!game_loaded) throw std::runtime_error("Open a ROM before loading cheats");
    auto imported = read_retroarch_cheats(path);
    clear_cheats();
    cheat_entries = std::move(imported);
    for (unsigned i = 0; i < cheat_entries.size(); ++i) {
        if (cheat_entries[i].enabled && !set_cheat_enabled(i, true)) {
            cheat_entries[i].enabled = false;
            log("Could not enable " + cheat_entries[i].label + "\n", LogSource::Frontend, RETRO_LOG_WARN);
        }
    }
}

bool Core::add_cheat(const char *label, const char *code) {
    if (!game_loaded || !n64emu_cheat_validate(code)) return false;
    cheat_entries.push_back({*label ? label : "Cheat " + std::to_string(cheat_entries.size() + 1), code, false, true});
    return true;
}

bool Core::set_cheat_enabled(unsigned index, bool enabled) {
    auto &entry = cheat_entries.at(index);
    if (!entry.supported || !n64emu_cheat_set(index, enabled, enabled ? entry.code.c_str() : nullptr))
        return false;
    entry.enabled = enabled;
    return true;
}

void Core::clear_cheats() {
    n64emu_cheat_reset();
    cheat_entries.clear();
}
void Core::save_ram() {
    if (!game_loaded) return;
    size_t size = 0; void *data = n64emu_save_memory(&size);
    auto path = save_path; path += L".srm";
    if (data && size) write_file(path, data, size);
}
void Core::save_state(unsigned slot) { auto p = save_path; p += L".state" + std::to_wstring(slot); save_state(p); }
void Core::load_state(unsigned slot) { auto p = save_path; p += L".state" + std::to_wstring(slot); load_state(p); }
void Core::save_state(const fs::path &path) {
    if (!game_loaded) return;
    std::vector<uint8_t> state(n64emu_state_size());
    if (state.empty() || !n64emu_save_state(state.data(), state.size())) throw std::runtime_error("State serialization failed; run the ROM first");
    write_file(path, state.data(), state.size());
}
void Core::load_state(const fs::path &path) {
    if (!game_loaded) return;
    auto state = read_file(path);
    if (!n64emu_load_state(state.data(), state.size())) throw std::runtime_error("State load failed");
    audio.clear(); audio.dsp.reset(float(sample_rate));
}
void Core::restore_options() {
    std::map<std::string, std::string> saved_options;
    auto values = config_section(directory, "core_options");
    if (values) {
        if (!cJSON_IsObject(values.get())) throw std::runtime_error("Invalid core_options in n64emu.json");
        for (auto item = values->child; item; item = item->next) {
            if (!cJSON_IsString(item)) throw std::runtime_error("Invalid core option in n64emu.json");
            saved_options[item->string] = item->valuestring;
        }
    } else {
        std::ifstream file(directory / L"config" / L"n64emu.options");
        std::string key, value;
        while (file >> std::quoted(key) >> std::quoted(value)) saved_options[key] = value;
        Json migrated(cJSON_CreateObject(), cJSON_Delete);
        for (const auto &entry : saved_options)
            cJSON_AddStringToObject(migrated.get(), entry.first.c_str(), entry.second.c_str());
        config_write_section(directory, "core_options", migrated.get());
    }
    std::vector<Option> current;
    const auto count = n64emu_option_count();
    current.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        n64emu_option definition{};
        if (!n64emu_get_option(i, &definition))
            continue;
        Option option;
        option.key = definition.key; option.label = definition.label;
        option.info = definition.info ? definition.info : "";
        option.category = definition.category ? definition.category : "";
        option.value = definition.value;
        for (unsigned j = 0; j < definition.count; ++j) {
            option.values.emplace_back(definition.values[j]);
            option.value_labels.emplace_back(definition.labels[j]);
        }
        auto saved = saved_options.find(option.key);
        if (saved != saved_options.end() && n64emu_set_option(option.key.c_str(), saved->second.c_str()))
            option.value = saved->second;
        current.push_back(std::move(option));
    }
    options = std::move(current);
    option_categories.clear();
    const auto category_count = n64emu_option_category_count();
    option_categories.reserve(category_count);
    for (unsigned i = 0; i < category_count; ++i) {
        n64emu_option_category category{};
        if (n64emu_get_option_category(i, &category))
            option_categories.push_back({category.key, category.label, category.info ? category.info : ""});
    }
}
void Core::save_options() {
    Json values(cJSON_CreateObject(), cJSON_Delete);
    for (const auto &option : options)
        cJSON_AddStringToObject(values.get(), option.key.c_str(), option.value.c_str());
    config_write_section(directory, "core_options", values.get());
}
void Core::set_option(Option &option, const std::string &value) {
    if (std::find(option.values.begin(), option.values.end(), value) == option.values.end() ||
        (initialized && !n64emu_set_option(option.key.c_str(), value.c_str())))
        return;
    option.value = value;
    save_options();
}

#include "core.h"
#include "shader_chain.h"
#include <fstream>
#include <iomanip>
#include <sstream>

Core::Core(Video &v, Input &i, const fs::path &dir) : video(v), input(i), directory(dir) {
    for (auto name : {L"system", L"saves", L"config"}) fs::create_directories(dir / name);
    input.load_configuration(dir);
}
Core::~Core() { try { unload(); } catch (...) {} }
void Core::log(const std::string &text, LogSource source, retro_log_level level) { append_log(text, source, level); }
void Core::load(const fs::path &content) {
    unload();
    content_path = fs::absolute(content);
    save_path = directory / L"saves" / L"n64emu";
    fs::create_directories(save_path);
    save_path /= content_path.stem();
    restore_options();
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
        option_categories = {{"emulation", "Emulation", "CPU accuracy"}, {"video", "Video", "SoftRDP presentation"}, {"input", "Controllers", "Controller accessories"}};
        for (unsigned i = 0; i < n64emu_option_count(); ++i) {
            n64emu_option definition{};
            n64emu_get_option(i, &definition);
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
            options.push_back(std::move(option));
        }
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
    input.end_core();
    options.clear(); option_categories.clear(); callback_error.clear(); title.clear();
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
    for (unsigned port = 0; port < 4; ++port)
        n64emu_set_controller(port, input.profile.players[port].controller != RETRO_DEVICE_NONE);
    n64emu_run();
    if (!callback_error.empty()) throw std::runtime_error(callback_error);
    if (++frames % 600 == 0) save_ram();
}
void Core::reset() { if (game_loaded) { n64emu_reset(); audio.clear(); audio.dsp.reset(float(sample_rate)); } }
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
    saved_options.clear();
    std::ifstream file(directory / L"config" / L"n64emu.options");
    std::string key, value;
    while (file >> std::quoted(key) >> std::quoted(value)) saved_options[key] = value;
}
void Core::save_options() {
    std::ostringstream text;
    for (const auto &o : options) text << std::quoted(o.key) << ' ' << std::quoted(o.value) << '\n';
    auto value = text.str();
    write_file(directory / L"config" / L"n64emu.options", value.data(), value.size());
}
void Core::set_option(Option &option, const std::string &value) {
    if (!n64emu_set_option(option.key.c_str(), value.c_str())) return;
    option.value = value;
    save_options();
}

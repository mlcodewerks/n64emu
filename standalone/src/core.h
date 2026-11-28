#pragma once
#include "audio.h"
#include "common.h"
#include "input.h"
#include "log.h"
#include "video.h"
#include "core/api.h"
struct Option {
    std::string key, label, info, value;
    std::vector<std::string> values, value_labels;
    std::string category, categorized_label, categorized_info;
    bool visible = true;
};
struct OptionCategory { std::string key, label, info; };
struct CheatEntry {
    std::string label, code;
    bool enabled = false, supported = false;
};
std::vector<CheatEntry> read_retroarch_cheats(const fs::path &path);
class Core {
  public:
    Core(Video &, Input &, const fs::path &);
    ~Core();
    void load(const fs::path &content);
    void unload();
    void run();
    void reset();
    void save_state(unsigned slot);
    void load_state(unsigned slot);
    void save_state(const fs::path &path);
    void load_state(const fs::path &path);
    void load_cheats(const fs::path &path);
    bool add_cheat(const char *label, const char *code);
    bool set_cheat_enabled(unsigned index, bool enabled);
    void clear_cheats();
    const std::vector<CheatEntry> &cheats() const { return cheat_entries; }
    void set_option(Option &, const std::string &);
    bool loaded() const { return game_loaded; }
    const std::string &name() const { return title; }
    double fps = 60;
    bool paused = false, audio_enabled = true;
    Audio audio;
    std::vector<Option> options;
    std::vector<OptionCategory> option_categories;
    void log(const std::string &, LogSource = LogSource::Frontend, retro_log_level = RETRO_LOG_INFO);
    uint64_t frames = 0;
  private:
    Video &video;
    Input &input;
    fs::path directory, content_path, save_path;
    std::string title, callback_error;
    std::vector<CheatEntry> cheat_entries;
    bool initialized = false, game_loaded = false;
    double sample_rate = 44100;
    void apply_av(const n64emu_av &);
    void save_ram();
    void restore_options();
    void save_options();
};

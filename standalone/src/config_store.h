#pragma once
#include "common.h"
#include "cJSON.h"
#include <memory>

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json config_read(const fs::path &directory);
Json config_section(const fs::path &directory, const char *section);
void config_write_section(const fs::path &directory, const char *section, const cJSON *value);
void config_initialize(const fs::path &directory);
struct InputProfile;
struct dsp_settings;
struct shader_settings;
bool config_input_exists(const fs::path &directory, const fs::path &legacy);
bool config_input_load(const fs::path &directory, const fs::path &legacy, InputProfile &, std::string &error);
bool config_input_save(const fs::path &directory, const fs::path &legacy, const InputProfile &, std::string &error);
bool config_dsp_load(const fs::path &directory, dsp_settings &, std::string &error);
bool config_dsp_save(const fs::path &directory, const dsp_settings &, std::string &error);
bool config_shader_load(const fs::path &directory, shader_settings &, std::string &error);
bool config_shader_save(const fs::path &directory, const shader_settings &, std::string &error);

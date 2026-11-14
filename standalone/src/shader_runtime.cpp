#include "shader_runtime.h"
#include "native.h"
#include <algorithm>
#include <cmath>
#include <fstream>

shader_runtime::chain::~chain()
{
    auto &api = librashader();
    std::string ignored;
    if (d3d9)
        api.check(api.d3d9_filter_chain_free(&d3d9), ignored);
}
void shader_runtime::clear()
{
    chains.clear();
    attempted = applied = 0;
    frame = 0;
    timestamp = 0;
}
bool shader_runtime::prepare(IDirect3DDevice9 *device)
try
{
    auto &control = video_shaders();
    control.init();
    auto &api = librashader();
    bool available = api.d3d9_available;
    if (!available || !control.settings.enabled) return false;
    if (attempted == control.revision)
        return !chains.empty();
    attempted = control.revision;
    std::vector<std::unique_ptr<chain>> next;
    for (size_t i = 0; i < control.settings.chains.size(); ++i)
    {
        auto &config = control.settings.chains[i];
        if (!config.enabled || std::none_of(config.passes.begin(), config.passes.end(), [](const shader_pass &p)
                                            { return p.enabled; }))
            continue;
        auto path = shader_temporary_preset();
        if (!shader_export(path, config, control.error))
            return !chains.empty();
        libra_shader_preset_t preset = nullptr;
        bool ok = api.check(api.preset_create(path.u8string().c_str(), &preset), control.error);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        if (!ok)
        {
            control.compilation_failed(config);
            return !chains.empty();
        }
        auto instance = std::make_unique<chain>();
        instance->index = i;
        libra_preset_param_list_t params{};
        ok = api.check(api.preset_get_runtime_params(&preset, &params), control.error);
        if (ok)
        {
            for (uint64_t p = 0; p < params.length; ++p)
            {
                float value = params.parameters[p].initial;
                api.check(api.preset_get_param(&preset, params.parameters[p].name, &value), control.error);
                instance->parameters[params.parameters[p].name] = value;
            }
            api.check(api.preset_free_runtime_params(params), control.error);
            {
                filter_chain_d3d9_opt_t options{};
                options.version = LIBRASHADER_CURRENT_VERSION;
                ok = api.check(api.d3d9_filter_chain_create(&preset, device, &options, &instance->d3d9), control.error);
            }
        }
        if (preset)
        {
            std::string cleanup;
            api.check(api.preset_free(&preset), cleanup);
        }
        if (!ok)
        {
            control.compilation_failed(config);
            return !chains.empty();
        }
        next.push_back(std::move(instance));
    }
    chains.swap(next);
    applied = attempted;
    frame = 0;
    timestamp = 0;
    control.error.clear();
    control.diagnostics.clear();
    return !chains.empty();
}
catch (const std::exception &e)
{
    video_shaders().error = e.what();
    return !chains.empty();
}
void shader_runtime::update_parameters()
{
    auto &control = video_shaders();
    auto &api = librashader();
    if (applied != control.revision)
        return; // Failed structural edits retain the complete previous state.
    for (auto &chain : chains)
        for (auto &parameter : chain->parameters)
        {
            if (chain->normalizes_input)
                continue;
            const auto &values = control.settings.chains[chain->index].values;
            auto found = values.find(parameter.first);
            if (found == values.end() || found->second == parameter.second || !std::isfinite(found->second))
                continue;
            libra_error_t error;
            error = api.d3d9_filter_chain_set_param(&chain->d3d9, parameter.first.c_str(), found->second);
            if (api.check(error, control.error))
                parameter.second = found->second;
        }
}

#include "config_store.h"
#include "IconsForkAwesome.h"
#include "core.h"
#include "core_options_window.h"
#include "dsp_chain_window.h"
#include "frame_pacing.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "input_window.h"
#include "log_window.h"
#include "native.h"
#include "rom_browser.h"
#include "shader_chain.h"
#include "shader_chain_window.h"
#include "shader_library.h"
#include "shader_source_editor.h"
#include "ui_fonts.h"
#include "imgui_theme.h"
#include "ui_window_layout.h"
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <shellapi.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace {
Input *input = nullptr;
bool rescan_input = false;
std::vector<fs::path> dropped;
constexpr int ui_scale_percentages[] = {0, 50, 75, 100, 125, 150, 175, 200};
constexpr const char *ui_scale_labels[] = {"Auto (fit window)", "50%", "75%", "100%", "125%", "150%", "175%", "200%"};

void fit_window_to_monitor(HWND window, const RECT &bounds) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    const auto &area = monitor.rcWork;
    const LONG width = std::min(bounds.right - bounds.left, area.right - area.left);
    const LONG height = std::min(bounds.bottom - bounds.top, area.bottom - area.top);
    const LONG x = std::clamp(bounds.left, area.left, area.right - width);
    const LONG y = std::clamp(bounds.top, area.top, area.bottom - height);
    SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui::GetCurrentContext())
        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
    switch (message) {
    case WM_DPICHANGED:
        fit_window_to_monitor(window, *reinterpret_cast<const RECT *>(lparam));
        return 0;
    case WM_CHAR:
        if (input)
            input->character(wchar_t(wparam));
        break;
    case WM_TOUCH:
        if (input) {
            input->touch(wparam, lparam);
            return 0;
        }
        break;
    case WM_INPUT:
        if (input)
            input->raw(lparam);
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_ACTIVATEAPP:
        if (input)
            input->focus(wparam != 0);
        break;
    case WM_DEVICECHANGE:
        rescan_input = true;
        break;
    case WM_DROPFILES: {
        auto drop = reinterpret_cast<HDROP>(wparam);
        unsigned count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
        for (unsigned i = 0; i < count; ++i) {
            UINT chars = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(chars + 1, 0);
            DragQueryFileW(drop, i, path.data(), chars + 1);
            path.resize(chars);
            dropped.emplace_back(path);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
struct FilePicker {
    RomBrowser browser;
    std::function<void(const fs::path &)> accept;
    void open(const std::string &title, const std::string &filters, const fs::path &directory, bool save,
              std::function<void(const fs::path &)> action) {
        BrowserConfig config;
        config.path = directory.u8string();
        config.flags = save ? BrowserFlags_ConfirmOverwrite : BrowserFlags_None;
        accept = std::move(action);
        browser.OpenDialog("FilePicker", title, filters.c_str(), config);
    }
    void draw() {
        if (browser.Display("FilePicker")) {
            const bool ok = browser.IsOk();
            auto path = fs::u8path(browser.GetFilePathName());
            auto action = std::move(accept);
            browser.Close();
            if (ok && action)
                action(path);
        }
    }
};
struct Timer {
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
    Timer() {
        if (!handle)
            handle = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    ~Timer() {
        if (handle)
            CloseHandle(handle);
    }
    void until(uint64_t deadline) {
        auto now = native_nanoseconds();
        if (deadline > now + 200000 && handle) {
            LARGE_INTEGER due;
            due.QuadPart = -LONGLONG((deadline - now - 100000) / 100);
            if (SetWaitableTimer(handle, &due, 0, nullptr, nullptr, FALSE))
                WaitForSingleObject(handle, 100);
        }
        while (native_nanoseconds() < deadline)
            YieldProcessor();
    }
};
} // namespace
int main() {
    init_command_line_output();
    int argc = 0;
    auto **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool hidden = false, no_audio = false, verify = false;
    uint64_t limit = 0, capture_interval = 0;
    fs::path content, capture, shader;
    std::string failure;
    try {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            auto value = [&]() -> std::wstring {
                if (++i >= argc)
                    throw std::runtime_error("Missing command-line value");
                return argv[i];
            };
            if (arg == L"--rom" || arg == L"-r")
                content = value();
            else if (arg == L"--context") {
                if (value() != L"d3d9") throw std::runtime_error("Only Direct3D 9 is supported");
            } else if (arg == L"--frames")
                limit = std::stoull(value());
            else if (arg == L"--hidden")
                hidden = true;
            else if (arg == L"--no-audio")
                no_audio = true;
            else if (arg == L"--verify-video")
                verify = true;
            else if (arg == L"--capture")
                capture = value();
            else if (arg == L"--capture-interval")
                capture_interval = std::stoull(value());
            else if (arg == L"--shader")
                shader = value();
            else if (arg == L"--help") {
                append_log(
                    "n64emu [--rom ROM] [--frames N] "
                    "[--hidden] [--no-audio] [--verify-video] [--capture OUTPUT.bmp] [--capture-interval "
                    "N]\n");
                LocalFree(argv);
                return 0;
            } else if (!arg.empty() && arg.front() != L'-' && content.empty())
                content = arg;
            else
                throw std::runtime_error("Unknown argument: " + narrow(arg));
        }
        LocalFree(argv);
        argv = nullptr;
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Initialize UI COM");
        ImGui_ImplWin32_EnableDpiAwareness();
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = window_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"n64emu";
        if (!RegisterClassExW(&wc))
            throw std::runtime_error("Window registration failed");
        HWND window =
            CreateWindowExW(0, wc.lpszClassName, L"n64emu", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);
        if (!window)
            throw std::runtime_error("Window creation failed");
        RECT window_bounds;
        if (GetWindowRect(window, &window_bounds))
            fit_window_to_monitor(window, window_bounds);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        init_ui_fonts();
        shader_source_editor_init_font();
        auto directory = executable_directory();
        config_initialize(directory);
        ImGui::GetIO().IniFilename = nullptr;
        auto layout = config_section(directory, "window_layout");
        if (layout) {
            if (!cJSON_IsString(layout.get())) throw std::runtime_error("Invalid window_layout in n64emu.json");
            ImGui::LoadIniSettingsFromMemory(layout->valuestring);
        } else if (fs::exists(directory / L"imgui.ini")) {
            auto bytes = read_file(directory / L"imgui.ini");
            ImGui::LoadIniSettingsFromMemory(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        }
        auto save_layout = [&] {
            Json value(cJSON_CreateString(ImGui::SaveIniSettingsToMemory()), cJSON_Delete);
            config_write_section(directory, "window_layout", value.get());
            ImGui::GetIO().WantSaveIniSettings = false;
        };
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        if (!ImGui_ImplWin32_Init(window))
            throw std::runtime_error("ImGui Win32 initialization failed");
        {
            Video video;
            video.verify_pixels = verify;
            video.init(window);
            if (!shader.empty()) {
                auto &control = video_shaders();
                control.init();
                shader_chain_config chain;
                if (!shader_import(shader, chain, control.error) || !control.inspect(chain))
                    throw std::runtime_error(control.error.empty() ? librashader().status : control.error);
                control.settings = {true, {chain}};
                control.changed();
            }
            Input controls;
            controls.init(window);
            input = &controls;
            DragAcceptFiles(window, TRUE);
            Core core(video, controls, directory);
            core.audio_enabled = !no_audio;
            dsp_settings dsp;
            std::string dsp_error;
            if (!config_dsp_load(directory, dsp, dsp_error) || !core.audio.dsp.configure(dsp, dsp_error))
                core.log(dsp_error + "\n");
            FilePicker picker;
            dsp_chain_window dsp_window;
            bool running = true, settings = false, logs = false, fast_forward = false;
            unsigned slot = 0;
            int ui_scale_choice = 0;
            float applied_ui_scale = 0;
            auto frontend = config_section(directory, "frontend");
            if (frontend) {
                auto scale = cJSON_GetObjectItemCaseSensitive(frontend.get(), "integer_scale");
                auto pixels = cJSON_GetObjectItemCaseSensitive(frontend.get(), "square_pixels");
                auto ui_scale = cJSON_GetObjectItemCaseSensitive(frontend.get(), "ui_scale_percent");
                auto saved_slot = cJSON_GetObjectItemCaseSensitive(frontend.get(), "state_slot");
                if (!cJSON_IsObject(frontend.get()) || !cJSON_IsBool(scale) || !cJSON_IsNumber(saved_slot) ||
                    (pixels && !cJSON_IsBool(pixels)) ||
                    saved_slot->valuedouble < 0 || saved_slot->valuedouble > 9 ||
                    saved_slot->valuedouble != std::floor(saved_slot->valuedouble))
                    throw std::runtime_error("Invalid frontend settings in n64emu.json");
                video.integer_scale = cJSON_IsTrue(scale);
                video.square_pixels = cJSON_IsTrue(pixels);
                slot = unsigned(saved_slot->valuedouble);
                if (ui_scale) {
                    const auto *end = std::end(ui_scale_percentages);
                    const auto *choice = std::find(std::begin(ui_scale_percentages), end, ui_scale->valueint);
                    if (!cJSON_IsNumber(ui_scale) || ui_scale->valuedouble != ui_scale->valueint || choice == end)
                        throw std::runtime_error("Invalid UI scale in n64emu.json");
                    ui_scale_choice = int(choice - std::begin(ui_scale_percentages));
                }
            }
            auto save_frontend = [&] {
                Json value(cJSON_CreateObject(), cJSON_Delete);
                cJSON_AddBoolToObject(value.get(), "integer_scale", video.integer_scale);
                cJSON_AddBoolToObject(value.get(), "square_pixels", video.square_pixels);
                cJSON_AddNumberToObject(value.get(), "ui_scale_percent", ui_scale_percentages[ui_scale_choice]);
                cJSON_AddNumberToObject(value.get(), "state_slot", slot);
                config_write_section(directory, "frontend", value.get());
            };
            uint64_t iteration = 0;
            Timer timer;
            FramePacing pacing;
            pacing.reset(native_nanoseconds());
            std::function<void()> pending;
            auto select_content = [&](const fs::path &path) {
                content = path;
                pending = [&, path] { core.load(path); };
            };
            auto browse_content = [&] {
                picker.open("Open Nintendo 64 ROM", "N64 ROMs{.z64,.n64,.v64,.bin,.u1}",
                            content.empty() ? directory : fs::absolute(content).parent_path(), false,
                            [&](const fs::path &path) { select_content(path); });
            };
            if (!content.empty()) select_content(content);
            if (!hidden)
                ShowWindow(window, SW_SHOWDEFAULT);
            while (running) {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message == WM_QUIT)
                        running = false;
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                if (!running)
                    break;
                if (rescan_input) {
                    controls.rescan();
                    rescan_input = false;
                }
                if (!dropped.empty()) {
                    auto path = dropped.front();
                    dropped.clear();
                    select_content(path);
                }
                if (pending) {
                    auto action = std::move(pending);
                    pending = {};
                    try {
                        action();
                        failure.clear();
                        pacing.reset(native_nanoseconds());
                    } catch (const std::exception &e) {
                        failure = e.what();
                        if (hidden)
                            throw;
                        core.log(failure + "\n");
                    }
                }
                int width, height;
                native_client_size(window, width, height);
                if (width > 0 && height > 0) {
                    const float scale = ui_scale_for_window(ImGui_ImplWin32_GetDpiScaleForHwnd(window),
                        width, height, ui_scale_percentages[ui_scale_choice]);
                    if (scale != applied_ui_scale) {
                        apply_imgui_theme(scale);
                        applied_ui_scale = scale;
                    }
                }
                video.new_frame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                const unsigned menu_height = unsigned(std::ceil(ImGui::GetFrameHeight()));
                const auto input_view = video.viewport(width, height, menu_height);
                controls.set_pointer_viewport({LONG(input_view.x), LONG(input_view.y),
                                               LONG(input_view.x + input_view.width),
                                               LONG(input_view.y + input_view.height)});
                const auto &io = ImGui::GetIO();
                controls.poll(io.WantCaptureKeyboard, io.WantCaptureMouse);
                try {
                    core.run();
                } catch (const std::exception &e) {
                    failure = e.what();
                    core.unload();
                    if (hidden)
                        throw;
                }
                video.draw(width, height, menu_height);
                if (!shader.empty() && !video_shaders().error.empty())
                    throw std::runtime_error(video_shaders().error);
                if (ImGui::BeginMainMenuBar()) {
                    if (ImGui::BeginMenu(ICON_FK_FOLDER_OPEN " File")) {
                        if (ImGui::MenuItem("Open Nintendo 64 ROM..."))
                            browse_content();
                        if (ImGui::MenuItem("Close ROM", nullptr, false, core.loaded()))
                            pending = [&] { core.unload(); };
                        if (ImGui::MenuItem("Exit"))
                            running = false;
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu(ICON_FK_GAMEPAD " Emulation", core.loaded())) {
                        if (ImGui::MenuItem("Pause", nullptr, &core.paused)) {
                            core.audio.clear();
                            pacing.reset(native_nanoseconds());
                        }
                        if (ImGui::MenuItem("Fast forward", nullptr, &fast_forward)) {
                            core.audio.set_frame_limit(!fast_forward);
                            pacing.reset(native_nanoseconds());
                        }
                        if (ImGui::MenuItem("Reset"))
                            pending = [&] { core.reset(); };
                        if (ImGui::BeginMenu("State slot")) {
                            for (unsigned i = 0; i < 10; ++i)
                                if (ImGui::MenuItem(std::to_string(i).c_str(), nullptr, i == slot))
                                {
                                    slot = i;
                                    save_frontend();
                                }
                            ImGui::EndMenu();
                        }
                        if (ImGui::MenuItem("Save state"))
                            pending = [&] { core.save_state(slot); };
                        if (ImGui::MenuItem("Load state"))
                            pending = [&] { core.load_state(slot); };
                        if (ImGui::MenuItem("Save state as..."))
                            picker.open("Save a savestate", ".state", directory / L"saves", true,
                                        [&](const fs::path &path) {
                                            pending = [&, path] { core.save_state(path); };
                                        });
                        if (ImGui::MenuItem("Load state from..."))
                            picker.open("Load a savestate", ".*", directory / L"saves", false,
                                        [&](const fs::path &path) {
                                            pending = [&, path] { core.load_state(path); };
                                        });
                        ImGui::EndMenu();
                    }
                    const struct {
                        const char *label;
                        bool *visible;
                    } tools[] = {
                        {ICON_FK_COG " Settings", &settings},
                        {"Log", &logs},
                    };
                    const float spacing = ImGui::GetStyle().ItemSpacing.x;
                    float tools_width = 0;
                    for (const auto &tool : tools)
                        tools_width += ImGui::CalcTextSize(tool.label).x + 2 * spacing;
                    const bool compact = tools_width > ImGui::GetContentRegionAvail().x;
                    if (!compact || ImGui::BeginMenu("Tools")) {
                        for (const auto &tool : tools)
                            ImGui::MenuItem(tool.label, nullptr, tool.visible);
                        if (compact) ImGui::EndMenu();
                    }
                    const char *title = core.loaded() ? core.name().c_str() : "n64emu - no ROM loaded";
                    if (ImGui::CalcTextSize(title).x + 3 * spacing < ImGui::GetContentRegionAvail().x) {
                        ImGui::Separator();
                        ImGui::TextUnformatted(title);
                    }
                    ImGui::EndMainMenuBar();
                }
                try {
                    picker.draw();
                } catch (const std::exception &e) {
                    failure = e.what();
                    core.log(failure + "\n");
                    if (hidden)
                        throw;
                }
                bool video_tab = false, audio_tab = false, input_tab = false;
                if (settings) {
                    ui_prepare_window("Settings", ImVec2(0.8f, 0.85f));
                    if (ImGui::Begin("Settings", &settings) && ImGui::BeginTabBar("settings")) {
                        if (ImGui::BeginTabItem("Video")) {
                            video_tab = true;
                            if (ImGui::BeginChild("Video settings", {}, 0, ImGuiWindowFlags_HorizontalScrollbar)) {
                                int aspect = video.square_pixels;
                                if (ImGui::Combo("Aspect ratio", &aspect, "Core aspect\0Square pixels\0")) {
                                    video.square_pixels = aspect != 0;
                                    save_frontend();
                                }
                                if (ImGui::Checkbox("Integer scaling", &video.integer_scale))
                                    save_frontend();
                                ImGui::SetItemTooltip("Scale by whole source scanlines. Square pixels uses whole multiples on both axes.");
                                ImGui::TextUnformatted("Renderer: Direct3D 9");
                                ImGui::SeparatorText("Shader Chains");
                                draw_shader_chain_settings();
                            }
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Interface")) {
                            if (ImGui::Combo("UI scale", &ui_scale_choice, ui_scale_labels, int(std::size(ui_scale_labels))))
                                save_frontend();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Audio")) {
                            audio_tab = true;
                            if (ImGui::BeginChild("Audio settings", {}, 0, ImGuiWindowFlags_HorizontalScrollbar)) {
                                ImGui::Text("WASAPI queue: %.1f ms", core.audio.queued_seconds() * 1000);
                                if (!core.audio.error().empty())
                                    ImGui::TextWrapped("%s", core.audio.error().c_str());
                                ImGui::SeparatorText("Audio DSP Chains");
                                dsp_window.draw_settings(core.audio.dsp, directory);
                            }
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Input")) {
                            input_tab = true;
                            draw_input_settings(controls, [&](unsigned player) {
                                draw_controller_pak_setting(core, player);
                            });
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Core")) {
                            if (ImGui::BeginChild("Core settings", {}, 0, ImGuiWindowFlags_HorizontalScrollbar))
                                draw_core_settings(core);
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                    ImGui::End();
                }
                if (!settings || !input_tab)
                    controls.cancel_capture();
                dsp_window.draw_picker(settings && audio_tab);
                draw_shader_chain_dialogs(settings && video_tab);
                draw_log_window(logs);
                ImGui::Render();
                if (ImGui::GetIO().WantSaveIniSettings)
                    save_layout();
                ++iteration;
                if (!capture.empty() && ((capture_interval && iteration % capture_interval == 0) ||
                                         (limit && iteration == limit))) {
                    auto path = capture;
                    if (capture_interval)
                        path = path.parent_path() / (path.stem().wstring() + L"-" +
                                                     std::to_wstring(iteration) + path.extension().wstring());
                    video.capture(path);
                }
                video.present();
                if (limit && iteration >= limit)
                    break;
                const auto now = native_nanoseconds();
                if (!fast_forward) {
                    const double fps = core.loaded() && !core.paused ? core.fps : 60;
                    timer.until(pacing.advance(now, fps));
                } else
                    pacing.reset(now);
            }
            if (verify && !video.verified_frames)
                throw std::runtime_error("No software video frames were verified");
            core.unload();
            input = nullptr;
            char result[256];
            std::snprintf(result, sizeof(result), "RESULT frames=%llu verified=%llu renderer=%s\n",
                          static_cast<unsigned long long>(iteration),
                          static_cast<unsigned long long>(video.verified_frames),
                          "d3d9");
            append_log(result);
        }
        save_layout();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        if (IsWindow(window))
            DestroyWindow(window);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        CoUninitialize();
        return 0;
    } catch (const std::exception &e) {
        if (argv)
            LocalFree(argv);
        append_log(std::string("ERROR: ") + e.what() + "\n", LogSource::Frontend, RETRO_LOG_ERROR, true);
        if (!hidden)
            MessageBoxW(nullptr, widen(e.what()).c_str(), L"n64emu", MB_ICONERROR);
        return 1;
    }
}

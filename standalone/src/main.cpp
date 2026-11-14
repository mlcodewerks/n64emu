#include "IconsForkAwesome.h"
#include "core.h"
#include "core_options_window.h"
#include "dsp_chain_window.h"
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
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <shellapi.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace {
Input *input = nullptr;
bool rescan_input = false;
std::vector<fs::path> dropped;
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui::GetCurrentContext())
        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
    switch (message) {
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
        if (deadline > now + 1000000 && handle) {
            LARGE_INTEGER due;
            due.QuadPart = -LONGLONG((deadline - now - 500000) / 100);
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
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        init_ui_fonts(ImGui_ImplWin32_GetDpiScaleForHwnd(window));
        shader_source_editor_init_font();
        auto directory = executable_directory();
        std::string ini = (directory / L"imgui.ini").u8string();
        ImGui::GetIO().IniFilename = ini.c_str();
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
            if (fs::exists(directory / L"winweg.dsp") &&
                dsp_load(directory / L"winweg.dsp", dsp, dsp_error)) {
                core.audio.dsp.configure(dsp, dsp_error);
            }
            FilePicker picker;
            dsp_chain_window dsp_window;
            bool running = true, settings = false, options = false, logs = false, fast_forward = false;
            unsigned slot = 0;
            uint64_t iteration = 0, deadline = native_nanoseconds();
            Timer timer;
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
                        deadline = native_nanoseconds();
                    } catch (const std::exception &e) {
                        failure = e.what();
                        if (hidden)
                            throw;
                        core.log(failure + "\n");
                    }
                }
                auto &io = ImGui::GetIO();
                int input_w = 0, input_h = 0;
                native_client_size(window, input_w, input_h);
                const auto input_view = video.viewport(std::max(1, input_w), std::max(1, input_h));
                controls.set_pointer_viewport({LONG(input_view.x), LONG(input_view.y),
                                               LONG(input_view.x + input_view.width),
                                               LONG(input_view.y + input_view.height)});
                controls.poll(io.WantCaptureKeyboard, io.WantCaptureMouse);
                try {
                    core.run();
                } catch (const std::exception &e) {
                    failure = e.what();
                    core.unload();
                    if (hidden)
                        throw;
                }
                video.new_frame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                int width, height;
                native_client_size(window, width, height);
                video.draw(std::max(1, width), std::max(1, height));
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
                        }
                        ImGui::MenuItem("Fast forward", nullptr, &fast_forward);
                        if (ImGui::MenuItem("Reset"))
                            pending = [&] { core.reset(); };
                        if (ImGui::BeginMenu("State slot")) {
                            for (unsigned i = 0; i < 10; ++i)
                                if (ImGui::MenuItem(std::to_string(i).c_str(), nullptr, i == slot))
                                    slot = i;
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
                    ImGui::MenuItem(ICON_FK_COG " Settings", nullptr, &settings);
                    ImGui::MenuItem("Audio DSP", nullptr, &dsp_window.visible);
                    ImGui::MenuItem("Shader chains", nullptr, &shader_window_visible);
                    ImGui::MenuItem("Core options", nullptr, &options, core.loaded());
                    ImGui::MenuItem("Log", nullptr, &logs);
                    ImGui::Separator();
                    ImGui::TextUnformatted(core.loaded() ? core.name().c_str() : "n64emu - no ROM loaded");
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
                if (settings) {
                    ImGui::SetNextWindowSize(ImVec2(580, 650), ImGuiCond_FirstUseEver);
                    ImGui::Begin("Settings", &settings);
                    if (ImGui::BeginTabBar("settings")) {
                        if (ImGui::BeginTabItem("Video")) {
                            ImGui::Checkbox("Integer scale", &video.integer_scale);
                            ImGui::TextUnformatted("Renderer: Direct3D 9");
                            if (ImGui::Button("Shader chains..."))
                                shader_window_visible = true;
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Audio")) {
                            if (ImGui::Button("Audio DSP..."))
                                dsp_window.visible = true;
                            ImGui::Text("WASAPI queue: %.1f ms", core.audio.queued_seconds() * 1000);
                            if (!core.audio.error().empty())
                                ImGui::TextWrapped("%s", core.audio.error().c_str());
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Input")) {
                            draw_input_settings(controls);
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                    ImGui::End();
                }
                dsp_window.draw(core.audio.dsp, directory);
                draw_shader_chain_window();
                draw_core_options(core, options);
                draw_log_window(logs);
                ImGui::Render();
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
                double fps = core.loaded() && !core.paused ? core.fps : 60;
                if (!fast_forward) {
                    double correction = 1;
                    if (core.audio.running() && !core.paused)
                        correction = std::clamp(
                            1 + (core.audio.queued_seconds() -
                                 0.08) *
                                    0.1,
                            0.995, 1.005);
                    deadline += uint64_t(1e9 / fps * correction);
                    auto now = native_nanoseconds();
                    if (deadline + 250000000 < now)
                        deadline = now;
                    timer.until(deadline);
                } else
                    deadline = native_nanoseconds();
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

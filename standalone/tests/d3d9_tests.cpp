#include "video.h"
#include "ui_fonts.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include <cstdio>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (ImGui::GetCurrentContext()) ImGui_ImplWin32_WndProcHandler(window, message, w, l);
    return DefWindowProcW(window, message, w, l);
}
int main(int argc, char **argv) {
    try {
        WNDCLASSW wc{};
        wc.lpfnWndProc = window_proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"n64emu-d3d9-test";
        if (!RegisterClassW(&wc)) throw std::runtime_error("Register test window");
        HWND window = CreateWindowW(wc.lpszClassName, L"D3D9 test", WS_OVERLAPPEDWINDOW,
            0, 0, 640, 480, nullptr, nullptr, wc.hInstance, nullptr);
        if (!window) throw std::runtime_error("Create test window");
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        init_ui_fonts(1);
        ImGui_ImplWin32_Init(window);
        {
            Video video;
            video.init(window); video.verify_pixels = true;
            video.start_core({13, 7, 13, 7, 13.f / 7});
            // Deliberately unaligned rows and nonzero X bits test row pitch,
            // channel order, orientation and opaque XRGB conversion on the GPU.
            constexpr size_t pitch = 13 * 4 + 3;
            std::vector<uint8_t> pixels(pitch * 7);
            for (unsigned y = 0; y < 7; ++y)
                for (unsigned x = 0; x < 13; ++x) {
                    uint32_t color = ((x * 19) << 16) | ((y * 35) << 8) | (x + y * 13);
                    std::memcpy(pixels.data() + y * pitch + x * 4, &color, 4);
                }
            auto frame = [&] {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg); DispatchMessageW(&msg);
                }
                video.new_frame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
                RECT rect{}; GetClientRect(window, &rect);
                video.draw(rect.right, rect.bottom);
                ImGui::Begin("D3D9 backend test"); ImGui::TextUnformatted("Embedded fonts and D3D9 rendering"); ImGui::End();
                ImGui::Render();
                if (argc > 1) video.capture(fs::u8path(argv[1]));
                video.present();
            };
            video.refresh(pixels.data(), 13, 7, pitch); frame();
            if (video.verified_frames != 1) throw std::runtime_error("Initial GPU upload not verified");
            video.refresh(nullptr, 13, 7, pitch); frame();
            if (video.verified_frames != 1) throw std::runtime_error("Duplicate frame uploaded again");
            for (auto size : {800, 480, 640}) {
                SetWindowPos(window, nullptr, 0, 0, size, size / 2, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                frame();
            }
            if (video.verified_frames != 4) throw std::runtime_error("Cached frame not restored across device resets");
            video.stop_core(); frame();
            video.start_core({13, 7, 13, 7, 13.f / 7});
            video.refresh(pixels.data(), 13, 7, pitch); frame();
            std::puts("PASS D3D9: GPU pixels, odd pitch, duplicate frames, resize/reset, reload, fonts and screenshot");
        }
        ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
        DestroyWindow(window); UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}

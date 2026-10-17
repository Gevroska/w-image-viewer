#include "pch.h"
#include "config.h"
#include "include/window_presentation.h"
#include <iostream>
#include <stdexcept>

namespace {
struct Child_window { DWORD process; HWND window = nullptr; };
BOOL CALLBACK find_child_window(HWND window, LPARAM parameter)
{
    auto& child = *reinterpret_cast<Child_window*>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    if (process == child.process && std::wstring_view(name) == L"WIV") {
        child.window = window;
        return FALSE;
    }
    return TRUE;
}
}

// Optional local acceptance test: requires a desktop with a DirectX 11 adapter.
int application_startup_tests(const std::filesystem::path& executable,
    const std::filesystem::path& fixtures, const std::filesystem::path& scratch)
{
    namespace fs = std::filesystem;
    int assertions = 0;
    const auto check = [&](bool condition, const char* message) {
        ++assertions;
        if (!condition) throw std::runtime_error(message);
    };
    for (int scenario = 0; scenario < 5; ++scenario) {
        const auto directory = scratch / ("startup-" + std::to_string(scenario));
        fs::create_directories(directory);
        const auto program = directory / "w-image-viewer.exe";
        fs::copy_file(executable, program, fs::copy_options::overwrite_existing);
        Config config{};
        config.window_width.val = 600; config.window_height.val = 400;
        config.window_maximize_on_open.val = scenario != 1;
        config.start_fullscreen.val = scenario == 2;
        config.write(directory);
        const bool valid_image = scenario != 3;
        const auto image = valid_image ? fixtures / "orientation-6.png" : directory / "missing.png";
        std::wstring command = L"\"" + program.wstring() + L"\" \"" + image.wstring() + L"\"";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = scenario == 4 ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE;
        PROCESS_INFORMATION process{};
        check(CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
            directory.c_str(), &startup, &process) != FALSE, "Could not launch viewer startup test");
        struct Close_child {
            PROCESS_INFORMATION process; HWND window = nullptr;
            ~Close_child() {
                if (window) PostMessageW(window, WM_CLOSE, 0, 0);
                if (WaitForSingleObject(process.hProcess, 5000) == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1);
                CloseHandle(process.hThread); CloseHandle(process.hProcess);
            }
        } close{process};
        Child_window child{process.dwProcessId};
        bool ready = false;
        for (int attempt = 0; attempt < 200; ++attempt) {
            check(WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT, "Viewer exited during startup test");
            EnumWindows(find_child_window, reinterpret_cast<LPARAM>(&child));
            if (child.window) {
                close.window = child.window;
                wchar_t title[512]{};
                GetWindowTextW(child.window, title, 512);
                if (IsWindowVisible(child.window) && (!valid_image || std::wstring_view(title).starts_with(L"orientation-6.png"))) {
                    ready = true; break;
                }
            }
            Sleep(100);
        }
        check(ready, "Viewer did not finish opening its startup image");
        if (scenario == 0) check(IsZoomed(child.window) != FALSE, "Startup file did not maximize the viewer");
        if (scenario == 1 || scenario == 3) {
            check(!IsZoomed(child.window) && !IsIconic(child.window), "Default-size startup unexpectedly maximized");
            RECT client{}; check(GetClientRect(child.window, &client) != FALSE, "Cannot read startup client size");
            check(client.right == 600 && client.bottom == 400, "Default dimensions were not preserved");
        }
        if (scenario == 2) {
            wiv::Monitor_coordinates physical;
            const auto monitor = wiv::window_monitor(child.window);
            RECT bounds{};
            check(monitor.valid() && GetWindowRect(child.window, &bounds), "Cannot inspect startup fullscreen");
            check(EqualRect(&bounds, &monitor.info.rcMonitor) != FALSE, "Startup fullscreen spans monitors");
            check((GetWindowLongPtrW(child.window, GWL_STYLE) & WS_POPUP) != 0, "Fullscreen did not take precedence over maximize");
        }
        if (scenario == 4) check(IsIconic(child.window) != FALSE, "Startup ignored an explicitly minimized launch");
        std::cout << "PASS: actual viewer startup scenario " << scenario << '\n';
    }
    return assertions;
}

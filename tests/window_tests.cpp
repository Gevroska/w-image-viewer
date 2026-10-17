#include "pch.h"
#include "include/window_presentation.h"
#include <iostream>
#include <stdexcept>

int window_presentation_tests()
{
    int assertions = 0;
    const auto check = [&](bool condition, const char* message) {
        ++assertions;
        if (!condition) throw std::runtime_error(message);
    };
    const auto previous_context = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    struct Restore_context { DPI_AWARENESS_CONTEXT previous; ~Restore_context() { if (previous) SetThreadDpiAwarenessContext(previous); } } restore{previous_context};
    WNDCLASSW type{};
    type.lpfnWndProc = DefWindowProcW;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"WivPresentationTests";
    check(RegisterClassW(&type) != 0, "Could not register test window");
    struct Unregister { HINSTANCE instance; ~Unregister() { UnregisterClassW(L"WivPresentationTests", instance); } } unregister{type.hInstance};

    std::vector<HMONITOR> monitors;
    {
        wiv::Monitor_coordinates physical;
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM parameter) -> BOOL {
            reinterpret_cast<std::vector<HMONITOR>*>(parameter)->push_back(monitor); return TRUE;
        }, reinterpret_cast<LPARAM>(&monitors));
    }
    check(!monitors.empty(), "No monitor available for integration tests");
    for (auto context : {DPI_AWARENESS_CONTEXT_UNAWARE, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2}) {
        SetThreadDpiAwarenessContext(context);
        for (HMONITOR handle : monitors) {
            const auto monitor = wiv::monitor_geometry(handle);
            check(monitor.valid(), "Could not query test monitor");
            const auto& work = monitor.info.rcWork;
            for (bool maximized : {false, true}) {
                HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName,
                    L"W Image Viewer isolated geometry test", WS_OVERLAPPEDWINDOW,
                    CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, nullptr, nullptr, type.hInstance, nullptr);
                check(window != nullptr, "Could not create test window");
                struct Destroy { HWND window; ~Destroy() { DestroyWindow(window); } } destroy{window};
                {
                    wiv::Monitor_coordinates physical;
                    check(SetWindowPos(window, nullptr, work.left + 20, work.top + 20,
                        std::min(640L, work.right - work.left - 40), std::min(480L, work.bottom - work.top - 40),
                        SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "Could not position test window");
                }
                ShowWindow(window, maximized ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE);
                WINDOWPLACEMENT before{}; before.length = sizeof(before);
                RECT normal{};
                LONG_PTR style;
                {
                    wiv::Monitor_coordinates physical;
                    check(GetWindowPlacement(window, &before) != FALSE, "Could not get initial placement");
                    check(GetWindowRect(window, &normal) != FALSE, "Could not get initial bounds");
                    style = GetWindowLongPtrW(window, GWL_STYLE);
                }
                wiv::Fullscreen_window fullscreen;
                for (int repeat = 0; repeat < 3; ++repeat) {
                    check(fullscreen.enter(window), "Could not enter fullscreen");
                    check(fullscreen.active(), "Fullscreen state was not committed");
                    {
                        wiv::Monitor_coordinates physical;
                        RECT actual{};
                        check(GetWindowRect(window, &actual) != FALSE, "Could not get fullscreen bounds");
                        check(EqualRect(&actual, &monitor.info.rcMonitor) != FALSE, "Fullscreen does not cover exactly the selected monitor");
                    }
                    fullscreen.refresh(window, false);
                    check(fullscreen.leave(window), "Could not restore fullscreen placement");
                    check(!fullscreen.active(), "Fullscreen state was not restored");
                    check(bool(IsZoomed(window)) == maximized, "Maximized state changed across fullscreen");
                    {
                        wiv::Monitor_coordinates physical;
                        RECT actual{};
                        check(GetWindowRect(window, &actual) != FALSE, "Could not get restored bounds");
                        check(EqualRect(&actual, &normal) != FALSE, "Restored window placement drifted");
                        check(GetWindowLongPtrW(window, GWL_STYLE) == style, "Window styles changed across fullscreen");
                    }
                }
                std::wcout << L"PASS: fullscreen round trips on " << monitor.info.szDevice
                    << L" [" << monitor.info.rcMonitor.left << L"," << monitor.info.rcMonitor.top << L"," << monitor.info.rcMonitor.right
                    << L"," << monitor.info.rcMonitor.bottom << L"] DPI " << GetDpiForWindow(window)
                    << (maximized ? L" maximized" : L" normal") << L'\n';
            }
        }
    }
    return assertions;
}

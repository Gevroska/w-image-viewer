#pragma once
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace wiv {
// Keep monitor queries and positioning in the same physical-coordinate context.
// This does not change the DPI awareness or scaling of the application's windows.
class Monitor_coordinates {
public:
    Monitor_coordinates() : previous_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~Monitor_coordinates() { if (previous_) SetThreadDpiAwarenessContext(previous_); }
    Monitor_coordinates(const Monitor_coordinates&) = delete;
    Monitor_coordinates& operator=(const Monitor_coordinates&) = delete;
private:
    DPI_AWARENESS_CONTEXT previous_;
};

struct Monitor_geometry {
    HMONITOR handle = nullptr;
    MONITORINFOEXW info{};
    bool valid() const noexcept { return handle != nullptr; }
};

inline Monitor_geometry monitor_geometry(HMONITOR monitor)
{
    Monitor_coordinates coordinates;
    Monitor_geometry result;
    result.info.cbSize = sizeof(result.info);
    if (monitor && GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&result.info))) result.handle = monitor;
    return result;
}

inline Monitor_geometry window_monitor(HWND window)
{
    Monitor_coordinates coordinates;
    return monitor_geometry(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST));
}

inline RECT auto_window_bounds(int image_width, int image_height, RECT work, RECT current,
                               int frame_width, int frame_height, bool center)
{
    const int work_width = work.right - work.left, work_height = work.bottom - work.top;
    const double scale = std::max(0.0, std::min({1.0,
        (work_width * 0.9 - frame_width) / std::max(image_width, 1),
        (work_height * 0.9 - frame_height) / std::max(image_height, 1)}));
    const int width = std::min(work_width, std::max(1, int(std::lround(image_width * scale)) + frame_width));
    const int height = std::min(work_height, std::max(1, int(std::lround(image_height * scale)) + frame_height));
    const LONG left = center ? work.left + (work_width - width) / 2
        : std::clamp(current.left, work.left, work.right - width);
    const LONG top = center ? work.top + (work_height - height) / 2
        : std::clamp(current.top, work.top, work.bottom - height);
    return {left, top, left + width, top + height};
}

class Fullscreen_window {
public:
    bool active() const noexcept { return active_; }
    bool transitioning() const noexcept { return transitioning_; }

    bool enter(HWND window)
    {
        if (active_) return true;
        Monitor_coordinates coordinates;
        const auto monitor = window_monitor(window);
        placement_ = {}; placement_.length = sizeof(placement_);
        if (!monitor.valid() || !GetWindowPlacement(window, &placement_)) return false;
        style_ = GetWindowLongPtrW(window, GWL_STYLE);
        ex_style_ = GetWindowLongPtrW(window, GWL_EXSTYLE);
        device_ = monitor.info.szDevice;
        active_ = transitioning_ = true;
        if (!set_style(window, GWL_STYLE, fullscreen_style())
            || !position(window, monitor.info.rcMonitor)) {
            set_style(window, GWL_STYLE, style_);
            SetWindowPlacement(window, &placement_);
            SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            active_ = transitioning_ = false;
            return false;
        }
        transitioning_ = false;
        return true;
    }

    bool leave(HWND window)
    {
        if (!active_) return true;
        Monitor_coordinates coordinates;
        transitioning_ = true;
        active_ = false;
        const bool restored = set_style(window, GWL_STYLE, style_)
            && set_style(window, GWL_EXSTYLE, ex_style_)
            && SetWindowPlacement(window, &placement_)
            && SetWindowPos(window, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (!restored) {
            active_ = true;
            set_style(window, GWL_STYLE, fullscreen_style());
            const auto monitor = window_monitor(window);
            if (monitor.valid()) position(window, monitor.info.rcMonitor);
        }
        transitioning_ = false;
        return restored;
    }

    void refresh(HWND window, bool follow_window)
    {
        if (!active_ || transitioning_ || IsIconic(window)) return;
        Monitor_coordinates coordinates;
        auto monitor = window_monitor(window);
        if (!follow_window) {
            Search search{device_};
            EnumDisplayMonitors(nullptr, nullptr, find_device, reinterpret_cast<LPARAM>(&search));
            if (search.monitor) monitor = monitor_geometry(search.monitor);
        }
        if (!monitor.valid()) return;
        device_ = monitor.info.szDevice;
        RECT current{};
        if (GetWindowRect(window, &current) && EqualRect(&current, &monitor.info.rcMonitor)) return;
        transitioning_ = true;
        position(window, monitor.info.rcMonitor);
        transitioning_ = false;
    }

private:
    LONG_PTR fullscreen_style() const noexcept
    {
        return (style_ & ~LONG_PTR(WS_OVERLAPPEDWINDOW | WS_MAXIMIZE | WS_MINIMIZE)) | WS_POPUP;
    }
    struct Search { std::wstring device; HMONITOR monitor = nullptr; };
    static BOOL CALLBACK find_device(HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
    {
        auto& search = *reinterpret_cast<Search*>(parameter);
        const auto geometry = monitor_geometry(monitor);
        if (geometry.valid() && search.device == geometry.info.szDevice) {
            search.monitor = monitor;
            return FALSE;
        }
        return TRUE;
    }
    static bool set_style(HWND window, int index, LONG_PTR style)
    {
        SetLastError(ERROR_SUCCESS);
        return SetWindowLongPtrW(window, index, style) != 0 || GetLastError() == ERROR_SUCCESS;
    }
    static bool position(HWND window, RECT bounds)
    {
        return SetWindowPos(window, nullptr, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOCOPYBITS) != FALSE;
    }
    bool active_ = false, transitioning_ = false;
    WINDOWPLACEMENT placement_{};
    LONG_PTR style_ = 0, ex_style_ = 0;
    std::wstring device_;
};
}

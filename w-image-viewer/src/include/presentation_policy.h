#pragma once

namespace wiv {
enum class Open_reason { Startup, ExplicitOpen, Navigation, Slideshow, SettingsReload };

inline bool maximize_on_open(Open_reason reason, bool enabled, bool fullscreen) noexcept
{
    return enabled && !fullscreen
        && (reason == Open_reason::Startup || reason == Open_reason::ExplicitOpen);
}

inline int long_edge_rotation(int image_width, int image_height, int monitor_width, int monitor_height) noexcept
{
    if (image_width <= 0 || image_height <= 0 || monitor_width <= 0 || monitor_height <= 0
        || image_width == image_height || monitor_width == monitor_height) return 0;
    return (image_width > image_height) != (monitor_width > monitor_height) ? 90 : 0;
}

struct Rotation_state {
    int angle = 0;
    bool manual_override = false;
    void reset(int automatic_angle) noexcept { angle = automatic_angle; manual_override = false; }
    void rotate(int delta) noexcept { angle = (angle + delta) % 360; manual_override = true; }
    bool update(int automatic_angle) noexcept
    {
        if (manual_override || angle == automatic_angle) return false;
        angle = automatic_angle;
        return true;
    }
};
}

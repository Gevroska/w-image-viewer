#pragma once
#include <algorithm>

namespace wiv {
class Navigation_visibility {
public:
    void reveal(double now) noexcept { last_activity = now; }
    float update(double now, bool activity, bool hovered_or_held, bool allowed) noexcept
    {
        if (!allowed) { was_hovered = false; return alpha = 0.0f; }
        if (activity || hovered_or_held || was_hovered) last_activity = now;
        was_hovered = hovered_or_held;
        return alpha = static_cast<float>(std::clamp(1.0 - (now - last_activity - 2.0) / 0.3, 0.0, 1.0));
    }
    float alpha = 0.0f;
private:
    double last_activity = -100.0;
    bool was_hovered = false;
};
}

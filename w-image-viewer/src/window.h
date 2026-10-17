#pragma once

#include "renderer.h"
#include "user_interface.h"
#include "file_manager.h"
#include "include/presentation_policy.h"

// Window styles.
inline constexpr auto WIV_WINDOW_STYLE = WS_OVERLAPPEDWINDOW;
inline constexpr auto WIV_WINDOW_EX_STYLE = WS_EX_ACCEPTFILES;

// Window messages.
inline constexpr auto WIV_WM_OPEN_FILE = WM_USER + 0;
inline constexpr auto WIV_WM_RESET_RESOURCES = WM_USER + 1;
inline constexpr auto WIV_WM_SHOW_CURSOR = WM_USER + 2;

class Window
{
public:
    Window(HINSTANCE hinstance, int ncmdshow, const std::wstring& startup_file = {});
    int message_loop();
    Renderer renderer;
    bool is_minimized = false;
private:
    static LRESULT wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void set_window_name() const;
    bool finish_image_open(wiv::Open_reason reason, bool show_window = true);
};

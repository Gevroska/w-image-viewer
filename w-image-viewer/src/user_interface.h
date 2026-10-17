#pragma once

#include "pch.h"
#include "file_manager.h"
#include "include\global.h"
#include "include/navigation_visibility.h"

enum WIV_OPEN_
{
    WIV_OPEN_IMAGE,
    WIV_OPEN_ICC
};

class User_interface
{
public:
    ~User_interface();
public:
    void create(ID3D11Device* device, ID3D11DeviceContext* device_context, bool* should_update);
    void update();
    void draw() const;
    void auto_window_size() const;
    void reset_image_panzoom() noexcept;
    void slideshow();
    void toggle_fullscreen();
    void navigate(int direction, bool automatic = false);
    void on_image_opened(bool automatic);
    void on_mouse_activity();
    bool navigation_visible() const noexcept { return navigation_visibility_.alpha > 0.0f; }
    File_manager file_manager;
    bool is_fullscreen = false;
    bool is_dialog_file_open = false;
    ImVec2 image_pan;
    bool is_panning = false;
    float image_zoom = 0;
    bool is_zooming = false;
    int image_rotation = 0;
    bool is_rotating = false;
    
    // Request to show image at the original width and height.
    bool image_no_scale = false;
    
private:
    void navigation_controls();
    void input();
    void overlay() const;
    void context_menu();
    void window_settings();
    void window_slideshow();
    void window_about();
    void dialog_file_open(WIV_OPEN_ file_type);
    bool is_overlay_open = g_config.overlay_show.val;
    bool is_window_settings_open = false;
    bool is_window_slideshow_open = false;
    bool is_window_about_open = false;
    bool* prenderer_should_update = nullptr;
    bool is_slideshow_start = false;
    bool is_slideshow_playing = false;
    wiv::Navigation_visibility navigation_visibility_;
    bool navigation_captures_mouse_ = false;
    bool canvas_drag_ = false;
};

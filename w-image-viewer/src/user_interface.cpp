#include "pch.h"
#include "user_interface.h"
#include "include\global.h"
#include "include\font.h"
#include "include\helpers.h"
#include "include\shader_config.h"
#include "resources\version.h"
#include "include\supported_extensions.h"
#include "window.h"
#include "include\info.h"
#include "include\ensure.h"

enum WIV_OVERLAY_SHOW_ : uint64_t
{
    WIV_OVERLAY_SHOW_IMAGE_DIMS = 1ull << 0,
    WIV_OVERLAY_SHOW_SCALE = 1ull << 1,
    WIV_OVERLAY_SHOW_SCALED_DIMS = 1ull << 2,
    WIV_OVERLAY_SHOW_KERNEL_INDEX = 1ull << 3,
    WIV_OVERLAY_SHOW_KERNEL_SIZE = 1ull << 4,
    WIV_OVERLAY_SHOW_KERNEL_RADIUS = 1ull << 5,
    WIV_OVERLAY_SHOW_IMAGE_BITDEPTH = 1ull << 6,
    WIV_OVERLAY_SHOW_IMAGE_NCHANNELS = 1ull << 7,
    WIV_OVERLAY_SHOW_SCALE_FILTER = 1ull << 8,
    WIV_OVERLAY_SHOW_KERNEL_SUPPORT = 1ull << 9
};

namespace
{
    // The order has to be same as in the enum WIV_KERNEL_FUNCTION_. 
    constexpr std::array kernel_function_names = {
        "Lanczos",
        "Ginseng",
        "Hamming",
        "Power of Cosine",
        "Kaiser",
        "Power of Garamond",
        "Power of Blackman",
        "GNW",
        "Said",
        "Nearest neighbor",
        "Linear",
        "Bicubic",
        "FSR kernel",
        "BC-Spline"
    };
}

User_interface::~User_interface()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void User_interface::create(ID3D11Device* device, ID3D11DeviceContext* device_context, bool* should_update)
{
    prenderer_should_update = should_update;

#ifndef NDEBUG
    IMGUI_CHECKVERSION();
#endif

    ImGui::CreateContext();

    // Font.
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesDefault());
    builder.AddChar(0x0161); // š
    ImVector<ImWchar> ranges;
    builder.BuildRanges(&ranges);
    ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(ROBOTO_MEDIUM_COMPRESSED_DATA, ROBOTO_MEDIUM_COMPRESSED_SIZE, 16, nullptr, ranges.Data);
    ImGui::GetIO().Fonts->Build();
    
    // Set imgui.ini path.
    // ImGui does not store the path!
    static char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::strcpy(path, (std::filesystem::path(path).parent_path() / "imgui.ini").string().c_str());
    ImGui::GetIO().IniFilename = path;
    
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(device, device_context);
}

void User_interface::update()
{
    // Feed inputs to dear imgui, start new frame.
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    overlay();
    context_menu();
    window_settings();
    window_about();
    window_slideshow();
    navigation_controls();
    input();

    // DEBUG ONLY!
    //

#if 0
    ImGui::ShowMetricsWindow();
#endif

#if 0
    ImGui::ShowDemoWindow();
#endif

    //

    ImGui::Render();
}

void User_interface::draw() const
{
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void User_interface::auto_window_size() const
{
    if (is_fullscreen() || IsZoomed(g_hwnd) || !file_manager.image.is_valid()) return;
    wiv::Monitor_coordinates coordinates;
    const auto monitor = wiv::window_monitor(g_hwnd);
    RECT current{}, frame{};
    if (!monitor.valid() || !GetWindowRect(g_hwnd, &current)) return;
    if (!AdjustWindowRectEx(&frame, GetWindowLongPtrW(g_hwnd, GWL_STYLE), FALSE,
                           GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE))) return;
    int width = file_manager.image.get_width<int>(), height = file_manager.image.get_height<int>();
    if (image_rotation() % 180) std::swap(width, height);
    const RECT bounds = wiv::auto_window_bounds(width, height, monitor.info.rcWork, current,
        frame.right - frame.left, frame.bottom - frame.top, g_config.window_autowh_center.val);
    SetWindowPos(g_hwnd, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
        bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

void User_interface::reset_image_panzoom() noexcept
{
    image_pan = ImVec2();
    image_zoom = 0.0f;
    image_no_scale = false;
    is_panning = is_zooming = is_rotating = false;
}

void User_interface::slideshow()
{
    if (!is_slideshow_playing || file_manager.file_current.empty()) {
        
        // We need to reset these in case slide show gets started while we dont have a file.
        is_slideshow_playing = false;
        is_slideshow_start = false;

        return;
    }
    static std::chrono::steady_clock::time_point start;
    
    // Upon slideshow start we need to set once duration for the first image (currently renderered image).
    if (is_slideshow_start) {
        start = std::chrono::steady_clock::now();
        is_slideshow_start = false;
    }

    if (std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count() < g_config.slideshow_interval.val) {
        return;
    }
    navigate(1, true);

    // This doesen't account for image load times.
    start = std::chrono::steady_clock::now();
}

void User_interface::navigate(int direction, bool automatic)
{
    const bool changed = direction < 0 ? file_manager.file_previous() : file_manager.file_next();
    if (changed) PostMessageW(g_hwnd, WIV_WM_OPEN_FILE, static_cast<WPARAM>(automatic ? wiv::Open_reason::Slideshow : wiv::Open_reason::Navigation), 0);
}

void User_interface::on_mouse_activity()
{
    if (ImGui::GetCurrentContext()) navigation_visibility_.reveal(ImGui::GetTime());
}

void User_interface::on_image_opened(bool automatic)
{
    if (!automatic) on_mouse_activity();
    if (is_slideshow_playing) is_slideshow_start = true;
}

void User_interface::navigation_controls()
{
    const auto& io = ImGui::GetIO();
    const auto* viewport = ImGui::GetMainViewport();
    const double now = ImGui::GetTime();
    const bool allowed = file_manager.image.is_valid() && GetForegroundWindow() == g_hwnd
        && !IsIconic(g_hwnd) && !is_dialog_file_open && !is_window_settings_open
        && !is_window_about_open && !is_window_slideshow_open
        && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const float dpi = float(GetDpiForWindow(g_hwnd)) / 96.0f;
    const ImVec2 size(48.0f * dpi, 64.0f * dpi);
    const float padding = 12.0f * dpi;
    const ImVec2 positions[] = {
        {viewport->Pos.x + padding, viewport->Pos.y + (viewport->Size.y - size.y) / 2},
        {viewport->Pos.x + viewport->Size.x - padding - size.x, viewport->Pos.y + (viewport->Size.y - size.y) / 2}
    };
    bool over_button = false;
    if (navigation_visibility_.alpha > 0.0f && !canvas_drag_) {
        for (const auto& position : positions)
            over_button |= io.MousePos.x >= position.x && io.MousePos.x < position.x + size.x
                && io.MousePos.y >= position.y && io.MousePos.y < position.y + size.y;
    }
    const bool mouse_inside = io.MousePos.x >= viewport->Pos.x && io.MousePos.x < viewport->Pos.x + viewport->Size.x
        && io.MousePos.y >= viewport->Pos.y && io.MousePos.y < viewport->Pos.y + viewport->Size.y;
    const bool moved = mouse_inside && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0);
    const float alpha = navigation_visibility_.update(now, moved, over_button, allowed);
    navigation_captures_mouse_ = false;
    if (alpha <= 0.0f) return;
    for (int side = 0; side < 2; ++side) {
        const int direction = side == 0 ? -1 : 1;
        ImGui::SetNextWindowPos(positions[side]);
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground
            | (canvas_drag_ ? ImGuiWindowFlags_NoInputs : 0);
        ImGui::Begin(side == 0 ? "##previous_image" : "##next_image", nullptr, flags);
        ImGui::BeginDisabled(!file_manager.can_navigate(direction));
        if (ImGui::InvisibleButton(side == 0 ? "Previous image" : "Next image", size)) navigate(direction);
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        navigation_captures_mouse_ |= !canvas_drag_ && (hovered || ImGui::IsItemActive()
            || (mouse_inside && io.MousePos.x >= positions[side].x && io.MousePos.x < positions[side].x + size.x
                && io.MousePos.y >= positions[side].y && io.MousePos.y < positions[side].y + size.y));
        if (hovered || ImGui::IsItemActive()) navigation_visibility_.reveal(now);
        auto* draw = ImGui::GetWindowDrawList();
        const auto p = positions[side];
        draw->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
            ImGui::GetColorU32(ImVec4(0.08f, 0.08f, 0.08f, hovered ? 0.9f : 0.65f)), 8 * dpi);
        const ImVec2 center(p.x + size.x / 2, p.y + size.y / 2);
        const float arrow = 9 * dpi;
        draw->AddTriangleFilled(ImVec2(center.x + direction * arrow / 2, center.y),
            ImVec2(center.x - direction * arrow / 2, center.y - arrow),
            ImVec2(center.x - direction * arrow / 2, center.y + arrow),
            ImGui::GetColorU32(ImVec4(1, 1, 1, 0.95f)));
        ImGui::EndDisabled();
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}

void User_interface::input()
{
    // Workaround for IsMouseDoubleClicked(0) triggering IsMouseDragging(0).
    static bool is_double_click;
    if (ImGui::IsMouseReleased(0))
        is_double_click = false;

    // Mouse
    //

    if (ImGui::IsMouseReleased(0)) canvas_drag_ = false;
    if (ImGui::IsMouseClicked(0) && !ImGui::GetIO().WantCaptureMouse && !navigation_captures_mouse_)
        canvas_drag_ = true;
    if ((!ImGui::GetIO().WantCaptureMouse && !navigation_captures_mouse_) || canvas_drag_) {
        if (ImGui::IsMouseDoubleClicked(0)) {
            toggle_fullscreen();
            is_double_click = true;
            return;
        }

        // Image panning.
        if (!is_double_click && ImGui::IsMouseDragging(0)) {
            image_pan += ImGui::GetMouseDragDelta();
            ImGui::ResetMouseDragDelta();
            is_panning = true;
            *prenderer_should_update = true;
            return;
        }

        // Image zoom.
        if (ImGui::GetIO().MouseWheel > 0.0f) {
            image_zoom += 0.1f;
            is_zooming = true;
            *prenderer_should_update = true;
            return;
        }
        if (ImGui::GetIO().MouseWheel < 0.0f) {
            image_zoom -= 0.1f;
            is_zooming = true;
            *prenderer_should_update = true;
            return;
        }
    }

    //

    // Keyboard
    //

    if (!ImGui::GetIO().WantCaptureKeyboard) {

        // ctrl + key
        //

        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
            if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                
                // It will block the thread, but probably no need to run it from new thread.
                dialog_file_open(WIV_OPEN_IMAGE);
                
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_1, false)) {
                reset_image_panzoom();
                image_no_scale = true;
                *prenderer_should_update = true;
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_0, false)) {
                reset_image_panzoom();
                *prenderer_should_update = true;
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
                image_zoom += 0.1f;
                is_zooming = true;
                *prenderer_should_update = true;
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
                image_zoom -= 0.1f;
                is_zooming = true;
                *prenderer_should_update = true;
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                rotate_image(-90);
                is_rotating = true;
                *prenderer_should_update = true;
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                rotate_image(90);
                is_rotating = true;
                *prenderer_should_update = true;
                return;
            }
        }

        //

        // alt + key
        //

        if (ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
                toggle_fullscreen();
                return;
            }
        }

        //

        // Free keys
        //

        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            if (file_manager.file_current.empty())
                return;
            navigate(-1);
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            if (file_manager.file_current.empty())
                return;
            navigate(1);
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
            toggle_fullscreen();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            file_manager.delete_file();
            return;
        }

        //

    }

    // Leave escape key with higher priority.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (is_fullscreen())
            toggle_fullscreen();
        else
            ensure(DestroyWindow(g_hwnd), != 0);
        return;
    }

    //
}

void User_interface::overlay() const
{
    if (!is_overlay_open) {
        return;
    }

    // Set overlay position.
    const auto viewport = ImGui::GetMainViewport();
    constexpr float pad = 6.0f;
    ImVec2 window_pos;
    window_pos.x = g_config.overlay_position.val & 1 ? viewport->WorkPos.x + viewport->WorkSize.x - pad : viewport->WorkPos.x + pad;
    window_pos.y = g_config.overlay_position.val & 2 ? viewport->WorkPos.y + viewport->WorkSize.y - pad : viewport->WorkPos.y + pad;
    ImVec2 window_pos_pivot;
    window_pos_pivot.x = g_config.overlay_position.val & 1 ? 1.0f : 0.0f;
    window_pos_pivot.y = g_config.overlay_position.val & 2 ? 1.0f : 0.0f;
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);

    ImGui::SetNextWindowBgAlpha(0.35f);
    if (ImGui::Begin("##overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!g_config.overlay_config.val) {
            ImGui::TextUnformatted("Overlay is not configured.");
        }
        if (g_config.overlay_config.val & WIV_OVERLAY_SHOW_IMAGE_DIMS) {
            ImGui::Text("Image W: %i", Info::image_width);
            ImGui::Text("Image H: %i", Info::image_height);
        }
        if (g_config.overlay_config.val & WIV_OVERLAY_SHOW_IMAGE_BITDEPTH) {
            ImGui::Text("Image bitdepth: %i", Info::image_bitdepth);
        }
        if (g_config.overlay_config.val & WIV_OVERLAY_SHOW_IMAGE_NCHANNELS) {
            ImGui::Text("Image nchannels: %i", Info::image_nchannels);
        }
        if (g_config.overlay_config.val & WIV_OVERLAY_SHOW_SCALED_DIMS) {
            ImGui::Text("Scaled W: %i", Info::scaled_width);
            ImGui::Text("Scaled H: %i", Info::scaled_height);
        }
        if (g_config.overlay_config.val & WIV_OVERLAY_SHOW_SCALE) {
            ImGui::Text("Scale: %.6f", Info::scale);
        }
        if (g_config.overlay_config.val & WIV_OVERLAY_SHOW_SCALE_FILTER) {
            ImGui::Text("Scale filter: %s", Info::scale_filter.c_str());
        }
        if (g_config.overlay_config.val & WIV_OVERLAY_SHOW_KERNEL_INDEX) {
            ImGui::Text("Kernel: %s", kernel_function_names[Info::kernel_index]);
        }
        if (g_config.overlay_config.val & WIV_OVERLAY_SHOW_KERNEL_SUPPORT) {
            ImGui::Text("Kernel support: %.6f", Info::kernel_support);
        }
        if (g_config.overlay_config.val & WIV_OVERLAY_SHOW_KERNEL_RADIUS) {
            ImGui::Text("Kernel radius: %i", Info::kernel_radius);
        }
        if (g_config.overlay_config.val & WIV_OVERLAY_SHOW_KERNEL_SIZE) {
            ImGui::Text("Kernel size: %i", Info::kernel_size);
        }
    }
    ImGui::End();
}

void User_interface::context_menu()
{
    if (ImGui::BeginPopupContextVoid("Right click menu")) {
        if (ImGui::Selectable("Open...")) {
            dialog_file_open(WIV_OPEN_IMAGE);
            goto end;
        }
        ImGui::Separator();
        if (ImGui::Selectable("Next")) {
            if (file_manager.file_current.empty())
                goto end;
            navigate(1);
            goto end;
        }
        if (ImGui::Selectable("Previous")) {
            if (file_manager.file_current.empty())
                goto end;
            navigate(-1);
            goto end;
        }
        ImGui::Separator();
        if (ImGui::Selectable("Fullscreen")) {
            toggle_fullscreen();
            goto end;
        }
        ImGui::Separator();
        if (ImGui::Selectable("Rotate CW")) {
            rotate_image(90);
            is_rotating = true;
            *prenderer_should_update = true;
            goto end;
        }
        if (ImGui::Selectable("Rotate CCW")) {
            rotate_image(-90);
            is_rotating = true;
            *prenderer_should_update = true;
            goto end;
        }
        ImGui::Separator();
        if (ImGui::Selectable("Delete")) {
            file_manager.delete_file();
            goto end;
        }
        ImGui::Separator();
        if (ImGui::Selectable("Overlay")) {
            is_overlay_open = !is_overlay_open;
            goto end;
        }
        ImGui::Separator();
        if (ImGui::Selectable("Slideshow...")) {
            is_window_slideshow_open = true;
            goto end;
        }
        if (ImGui::Selectable("Settings...")) {
            is_window_settings_open = true;
            goto end;
        }
        if (ImGui::Selectable("About...")) {
            is_window_about_open = true;
            goto end;
        }
        ImGui::Separator();
        if (ImGui::Selectable("Exit")) {
            ensure(DestroyWindow(g_hwnd), != 0);
            goto end;
        }
    end:
        ImGui::EndPopup();
    }
}

void User_interface::window_settings()
{
    // Early return.
    if (!is_window_settings_open)
        return;

    static constexpr ImVec2 window_size = { 430.0f, 696.0f };
    static constexpr ImVec2 button_size = { -1.0f, 0.0f };
    static int scale_profile_index; // The default profile should always be at index 0!
    ImGui::SetNextWindowSize(window_size, ImGuiCond_FirstUseEver);
    ImGui::Begin("Settings", &is_window_settings_open, ImGuiWindowFlags_NoCollapse);
    if (ImGui::CollapsingHeader("Window")) {
        ImGui::Spacing();
        ImGui::SeparatorText("Default dimensions");
        if (ImGui::Button("Use current dimensions##def")) {
            RECT rect;
            ensure(GetClientRect(g_hwnd, &rect), != 0);
            g_config.window_width.val = rect.right;
            g_config.window_height.val = rect.bottom;
        }
        ImGui::InputInt("Width##def", &g_config.window_width.val, 0, 0);
        ImGui::InputInt("Height##def", &g_config.window_height.val, 0, 0);
        ImGui::Spacing();
        ImGui::SeparatorText("Minimum dimensions");
        if (ImGui::Button("Use current dimensions##min")) {
            RECT rect;
            ensure(GetClientRect(g_hwnd, &rect), != 0);
            g_config.window_min_width.val = rect.right;
            g_config.window_min_height.val = rect.bottom;
        }
        ImGui::InputInt("Width##min", &g_config.window_min_width.val, 0, 0);
        ImGui::InputInt("Height##min", &g_config.window_min_height.val, 0, 0);
        ImGui::Spacing();
        ImGui::Checkbox("Start fullscreen", &g_config.start_fullscreen.val);
        ImGui::Checkbox("Maximize when opening a file", &g_config.window_maximize_on_open.val);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Applies to startup, Open and drop. Browsing preserves window state. Fullscreen takes precedence.");
        ImGui::Checkbox("Keep aspect ratio when sizing", &g_config.window_keep_aspect.val);
        ImGui::Checkbox("Auto dimensions", &g_config.window_autowh.val);
        ImGui::BeginDisabled(!g_config.window_autowh.val);
        ImGui::Checkbox("Center on auto dimensions", &g_config.window_autowh_center.val);
        ImGui::EndDisabled();
        ImGui::Spacing();

        // The order has to be same as in the enum WIV_WINDOW_NAME_.
        static constexpr std::array window_name_items = {
            "Defualt name",
            "Filename",
            "Full filename"
        };

        ImGui::Combo("Title", &g_config.window_name.val, window_name_items.data(), window_name_items.size());
        ImGui::Spacing();

        ImGui::ColorEdit4("Background color", g_config.clear_color.val.data(), ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_DisplayHSV);
        ImGui::Spacing();
    }
    if (ImGui::CollapsingHeader("Image orientation")) {
        ImGui::BeginDisabled(orientation_reload_pending_);
        bool metadata = g_config.image_apply_metadata_orientation.val;
        bool align = g_config.image_align_long_edge.val;
        bool changed = ImGui::Checkbox("Apply EXIF/camera orientation", &metadata);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Controls EXIF and camera tags. Native JPEG XL and HEIF/AVIF transformations remain applied.");
        ImGui::BeginDisabled(metadata);
        changed |= ImGui::Checkbox("Align long edge with the monitor", &align);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Uses the monitor shape, even in a narrow window. Manual rotation overrides alignment until the next image or preference change.");
        ImGui::EndDisabled();
        if (changed) request_orientation_preferences(metadata, align);
        ImGui::EndDisabled();
        if (!orientation_error_.empty()) ImGui::TextWrapped("%s", orientation_error_.c_str());
        ImGui::Spacing();
    }
    if (ImGui::CollapsingHeader("Scale")) {
        ImGui::Spacing();

        // Scale profiles
        //

        ImGui::SeparatorText("Profiles");
        static Right_open_range<float> range;
        std::array range_array = { &range.lower, &range.upper };
        ImGui::InputFloat2("##range", *range_array.data(), "%.6f");
        ImGui::SameLine();
        if (ImGui::Button("Add profile", button_size)) {
            if (range.is_valid()) {

                // Check first does profile already exists (overlaps with any other profile).
                bool exists = false;
                for (const auto& profile : g_config.scale_profiles) {
                    if (profile.range.is_overlapping(range)) {
                        exists = true;
                        break;
                    }
                }

                if (!exists) {
                    g_config.scale_profiles.push_back({ range, {} });
                    scale_profile_index = g_config.scale_profiles.size() - 1;
                }
                else
                    wiv_message(L"Overlapping profile. Profile overlaps with one of existing profiles.");
            }
            else
                wiv_message(L"Invalid range. Lower bound has to be lower or equal to upper bound.");
        }
        if (ImGui::Button("Edit profile", button_size)) {
            if (range.is_valid()) {

                // Check first is profile overlapping with any other profile.
                bool overlaps = false;
                for (int i = 0; i < g_config.scale_profiles.size(); ++i)
                    if (i != scale_profile_index && g_config.scale_profiles[i].range.is_overlapping(range)) {
                        overlaps = true;
                        break;
                    }

                if (scale_profile_index > 0 && !overlaps) // We don't wanna allow edits on the default profile, nor overlaps with other profiles!
                    g_config.scale_profiles[scale_profile_index].range = range;

            }
            else
                wiv_message(L"Invalid range. Lower bound has to be lower or equal to upper bound.");
        }

        // Hold values.
        // Can't use vector<const char*> directly, because const char* will be non owning.
        std::vector<std::string> scale_profile_names;
        for (const auto& scale_profile_name : g_config.scale_profiles)
            scale_profile_names.push_back(std::to_string(scale_profile_name.range.lower) + ", " + std::to_string(scale_profile_name.range.upper));

        // Can't use std::string directly in ImGui.
        std::vector<const char*> profile_items;
        for (const auto& profile_item : scale_profile_names)
            profile_items.push_back(profile_item.c_str());


        ImGui::Combo("##profile", &scale_profile_index, profile_items.data(), profile_items.size());
        ImGui::SameLine();
        if (ImGui::Button("Remove profile", button_size)) {
            if (scale_profile_index > 0) { // Can't delete the default range!
                g_config.scale_profiles.erase(g_config.scale_profiles.begin() + scale_profile_index);
                --scale_profile_index;
            }
        }
        auto& scale = g_config.scale_profiles[scale_profile_index].config;
        ImGui::Spacing();

        //

        ImGui::SeparatorText("Pre-scale blur (downscale only)");
        ImGui::Checkbox("Enable pre-scale blur", &scale.blur_use.val);
        ImGui::BeginDisabled(!scale.blur_use.val);
        ImGui::InputInt("Radius##blur", &scale.blur_radius.val, 0, 0);
        scale.blur_radius.val = std::max(scale.blur_radius.val, 1);
        ImGui::InputFloat("Sigma##blur", &scale.blur_sigma.val, 0.0f, 0.0f, "%.6f");
        scale.blur_sigma.val = std::max(scale.blur_sigma.val, 1e-6f);
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::SeparatorText("Sigmoidize (upscale only)");
        ImGui::Checkbox("Enable sigmoidize", &scale.sigmoid_use.val);
        ImGui::BeginDisabled(!scale.sigmoid_use.val);
        ImGui::InputFloat("Contrast", &scale.sigmoid_contrast.val, 0.0f, 0.0f, "%.6f");
        scale.sigmoid_contrast.val = std::max(scale.sigmoid_contrast.val, 1e-6f);
        ImGui::InputFloat("Midpoint", &scale.sigmoid_midpoint.val, 0.0f, 0.0f, "%.6f");
        scale.sigmoid_midpoint.val = std::clamp(scale.sigmoid_midpoint.val, 0.0f, 1.0f);
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::SeparatorText("Filter");
        ImGui::Checkbox("Use cylindrical filtering (Jinc based)", &scale.kernel_cylindrical_use.val);
        ImGui::Combo("Kernel-function", &scale.kernel_index.val, kernel_function_names.data(), kernel_function_names.size());
        bool has_fixed_radius = false;
        switch (scale.kernel_index.val) {
            case WIV_KERNEL_FUNCTION_NEAREST:
            case WIV_KERNEL_FUNCTION_LINEAR:
            case WIV_KERNEL_FUNCTION_BICUBIC:
            case WIV_KERNEL_FUNCTION_FSR:
            case WIV_KERNEL_FUNCTION_BCSPLINE:
                has_fixed_radius = true;
        }
        ImGui::BeginDisabled(has_fixed_radius);
        ImGui::InputFloat("Support##kernel", &scale.kernel_support.val, 0.0f, 0.0f, "%.6f");
        ImGui::InputFloat("Blur", &scale.kernel_blur.val, 0.0f, 0.0f, "%.6f");
        ImGui::EndDisabled();
        bool hasno_parameter1 = false;
        switch (scale.kernel_index.val) {
            case WIV_KERNEL_FUNCTION_LANCZOS:
            case WIV_KERNEL_FUNCTION_GINSENG:
            case WIV_KERNEL_FUNCTION_HAMMING:
            case WIV_KERNEL_FUNCTION_NEAREST:
            case WIV_KERNEL_FUNCTION_LINEAR:
            case WIV_KERNEL_FUNCTION_FSR:
                hasno_parameter1 = true;
        }
        ImGui::BeginDisabled(hasno_parameter1);
        ImGui::InputFloat("Parameter 1", &scale.kernel_parameter1.val, 0.0f, 0.0f, "%.6f");
        ImGui::EndDisabled();
        bool hasno_parameter2 = false;
        switch (scale.kernel_index.val) {
            case WIV_KERNEL_FUNCTION_LANCZOS:
            case WIV_KERNEL_FUNCTION_GINSENG:
            case WIV_KERNEL_FUNCTION_HAMMING:
            case WIV_KERNEL_FUNCTION_POW_COSINE:
            case WIV_KERNEL_FUNCTION_KAISER:
            case WIV_KERNEL_FUNCTION_NEAREST:
            case WIV_KERNEL_FUNCTION_LINEAR:
            case WIV_KERNEL_FUNCTION_BICUBIC:
            case WIV_KERNEL_FUNCTION_FSR:
                hasno_parameter2 = true;
        }
        ImGui::BeginDisabled(hasno_parameter2);
        ImGui::InputFloat("Parameter 2", &scale.kernel_parameter2.val, 0.0f, 0.0f, "%.6f");
        ImGui::EndDisabled();
        ImGui::InputFloat("Anti-ringing", &scale.kernel_antiringing.val, 0.0f, 0.0f, "%.6f");
        scale.kernel_antiringing.val = std::clamp(scale.kernel_antiringing.val, 0.0f, 1.0f);
        ImGui::Spacing();
        ImGui::SeparatorText("Post-scale unsharp mask");
        ImGui::Checkbox("Enable post-scale unsharp mask", &scale.unsharp_use.val);
        ImGui::BeginDisabled(!scale.unsharp_use.val);
        ImGui::InputInt("Radius##unsharp", &scale.unsharp_radius.val, 0, 0);
        scale.unsharp_radius.val = std::max(scale.unsharp_radius.val, 1);
        ImGui::InputFloat("Sigma##unsharp", &scale.unsharp_sigma.val, 0.0f, 0.0f, "%.6f");
        scale.unsharp_sigma.val = std::max(scale.unsharp_sigma.val, 1e-6f);
        ImGui::InputFloat("Amount", &scale.unsharp_amount.val, 0.0f, 0.0f, "%.6f");
        scale.unsharp_amount.val = std::max(scale.unsharp_amount.val, 1e-6f);
        ImGui::EndDisabled();
        ImGui::Spacing();
    }
    if (ImGui::CollapsingHeader("Color management")) {
        ImGui::Spacing();
        ImGui::Checkbox("Enable color management", &g_config.cms_use.val);
        ImGui::Spacing();
        ImGui::BeginDisabled(!g_config.cms_use.val);

        // The order has to be the same as in the enum WIV_CMS_PROFILE_DISPLAY_.
        static constexpr std::array cms_display_profile_items = {
            "Auto",
            "sRGB",
            "AdobeRGB",
            "Custom"
        };

        ImGui::Combo("Display profile", &g_config.cms_display_profile.val, cms_display_profile_items.data(), cms_display_profile_items.size());
        char buffer[MAX_PATH];
        std::strcpy(buffer, g_config.cms_display_profile_custom.val.string().c_str());
        ImGui::BeginDisabled(g_config.cms_display_profile.val != WIV_CMS_PROFILE_DISPLAY_CUSTOM);
        ImGui::InputText("##custom_path", buffer, MAX_PATH);
        g_config.cms_display_profile_custom.val = buffer;
        ImGui::SameLine();
        if (ImGui::Button("Custom...", button_size)) {
            dialog_file_open(WIV_OPEN_ICC);
        }
        ImGui::EndDisabled();
        ImGui::Spacing();

        // The order has to be the same as the lcms2 ICC Intents.
        static constexpr std::array cms_intent_items = {
            "Perceptual",
            "Relative colorimetric",
            "Saturation",
            "Absolute colorimetric"
        };

        ImGui::Combo("Rendering intent", &g_config.cms_intent.val, cms_intent_items.data(), cms_intent_items.size());
        ImGui::Checkbox("Black Point Compensation", &g_config.cms_bpc_use.val);

        // LUT size
        static constexpr std::array cms_lut_size_items = {
            "33",
            "49",
            "65",
        };
        int cms_lut_size_items_index;
        switch (g_config.cms_lut_size.val) {
            case 33:
                cms_lut_size_items_index = 0;
                break;
            case 49:
                cms_lut_size_items_index = 1;
                break;
            case 65:
                cms_lut_size_items_index = 2;
        }
        ImGui::Combo("LUT size", &cms_lut_size_items_index, cms_lut_size_items.data(), cms_lut_size_items.size());
        switch (cms_lut_size_items_index) {
            case 0:
                g_config.cms_lut_size.val = 33;
                break;
            case 1:
                g_config.cms_lut_size.val = 49;
                break;
            case 2:
                g_config.cms_lut_size.val = 65;
        }
        
        ImGui::Checkbox("Dither 8 bit images", &g_config.cms_dither.val);
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::SeparatorText("Color tags");
        ImGui::Checkbox("Linear tagged images default to ACEScg", &g_config.cms_default_to_aces.val);
        ImGui::Checkbox("Untagged images default to sRGB", &g_config.cms_default_to_srgb.val);
        ImGui::Spacing();
    }
    if (ImGui::CollapsingHeader("Transparency")) {
        ImGui::Spacing();
        ImGui::InputFloat("Tile size", &g_config.alpha_tile_size.val, 0.0f, 0.0f, "%.6f");
        ImGui::ColorEdit3("First tile color", g_config.alpha_tile1_color.val.data(), ImGuiColorEditFlags_DisplayHSV);
        ImGui::ColorEdit3("Second tile color", g_config.alpha_tile2_color.val.data(), ImGuiColorEditFlags_DisplayHSV);
        ImGui::Spacing();
    }
    if (ImGui::CollapsingHeader("Overlay")) {
        ImGui::Spacing();
        ImGui::Checkbox("Show overlay on start", &g_config.overlay_show.val);
        static constexpr std::array overlay_position_items = {
            "Top-left",
            "Top-right",
            "Bottom-left",
            "Bottom-right"
        };
        ImGui::Combo("Overlay position", &g_config.overlay_position.val, overlay_position_items.data(), overlay_position_items.size());
        ImGui::Spacing();
        ImGui::TextUnformatted("Show:");
        if (ImGui::Selectable("Image dimensions", g_config.overlay_config.val & WIV_OVERLAY_SHOW_IMAGE_DIMS)) {
            g_config.overlay_config.val ^= WIV_OVERLAY_SHOW_IMAGE_DIMS;
        }
        if (ImGui::Selectable("Image bit depth", g_config.overlay_config.val & WIV_OVERLAY_SHOW_IMAGE_BITDEPTH)) {
            g_config.overlay_config.val ^= WIV_OVERLAY_SHOW_IMAGE_BITDEPTH;
        }
        if (ImGui::Selectable("Image nchannels", g_config.overlay_config.val & WIV_OVERLAY_SHOW_IMAGE_NCHANNELS)) {
            g_config.overlay_config.val ^= WIV_OVERLAY_SHOW_IMAGE_NCHANNELS;
        }
        if (ImGui::Selectable("Scaled dimensions", g_config.overlay_config.val & WIV_OVERLAY_SHOW_SCALED_DIMS)) {
            g_config.overlay_config.val ^= WIV_OVERLAY_SHOW_SCALED_DIMS;
        }
        if (ImGui::Selectable("Scale factor", g_config.overlay_config.val & WIV_OVERLAY_SHOW_SCALE)) {
            g_config.overlay_config.val ^= WIV_OVERLAY_SHOW_SCALE;
        }
        if (ImGui::Selectable("Scale filter", g_config.overlay_config.val & WIV_OVERLAY_SHOW_SCALE_FILTER)) {
            g_config.overlay_config.val ^= WIV_OVERLAY_SHOW_SCALE_FILTER;
        }
        if (ImGui::Selectable("Kernel function", g_config.overlay_config.val & WIV_OVERLAY_SHOW_KERNEL_INDEX)) {
            g_config.overlay_config.val ^= WIV_OVERLAY_SHOW_KERNEL_INDEX;
        }
        if (ImGui::Selectable("Scale kernel support", g_config.overlay_config.val & WIV_OVERLAY_SHOW_KERNEL_SUPPORT)) {
            g_config.overlay_config.val ^= WIV_OVERLAY_SHOW_KERNEL_SUPPORT;
        }
        if (ImGui::Selectable("Scale kernel radius", g_config.overlay_config.val & WIV_OVERLAY_SHOW_KERNEL_RADIUS)) {
            g_config.overlay_config.val ^= WIV_OVERLAY_SHOW_KERNEL_RADIUS;
        }
        if (ImGui::Selectable("Scale kernel size", g_config.overlay_config.val & WIV_OVERLAY_SHOW_KERNEL_SIZE)) {
            g_config.overlay_config.val ^= WIV_OVERLAY_SHOW_KERNEL_SIZE;
        }
        ImGui::Spacing();
    }
    if (ImGui::CollapsingHeader("Other")) {
        ImGui::Spacing();
        ImGui::Checkbox("Read only thumbnails in RAW images", &g_config.raw_thumb.val);
        ImGui::Spacing();
        ImGui::Checkbox("Cycle files on Next/Previous", &g_config.cycle_files.val);
        ImGui::Spacing();
    }
    ImGui::SeparatorText("Changes");
    ImGui::BeginDisabled(orientation_reload_pending_);
    if (ImGui::Button("Revert changes", button_size)) {
        scale_profile_index = 0;
        const bool previous_metadata = g_config.image_apply_metadata_orientation.val;
        const bool previous_align = g_config.image_align_long_edge.val;
        g_config.read();
        const bool desired_metadata = g_config.image_apply_metadata_orientation.val;
        const bool desired_align = g_config.image_align_long_edge.val;
        g_config.image_apply_metadata_orientation.val = previous_metadata;
        g_config.image_align_long_edge.val = previous_align;
        request_orientation_preferences(desired_metadata, desired_align);
    }
    if (ImGui::Button("Write changes", button_size)) {
        g_config.write();
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::End();
}

void User_interface::window_slideshow()
{
    if (!is_window_slideshow_open)
        return;
    static constexpr ImVec2 size = { 288.0f, 0.0f };
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("Slideshow", &is_window_slideshow_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    if (ImGui::Checkbox("Auto close this window on start", &g_config.slideshow_auto_close.val)) {
        g_config.write_slideshow();
    }
    if (ImGui::InputFloat("Interval", &g_config.slideshow_interval.val, 0.0f, 0.0f, "%.3f")) {
        g_config.write_slideshow();
    }
    if (ImGui::Button("Start")) {
        if (g_config.slideshow_auto_close.val) {
            is_window_slideshow_open = false;
        }
        is_slideshow_start = true;
        is_slideshow_playing = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        is_slideshow_playing = false;
    }
    ImGui::End();
}

void User_interface::window_about()
{
    // Early return.
    if (!is_window_about_open)
        return;

    static constexpr ImVec2 size = { 288, 178.0f };
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("About", &is_window_about_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::Text("W Image Viewer %s", WIV_VERSION_STRING);
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted(reinterpret_cast<const char*>(u8"Created by Ivan Bjeliš."));
    ImGui::Spacing();
    ImGui::Spacing();
    if (ImGui::Button("Web page...")) {
        if (is_fullscreen())
            toggle_fullscreen();
        ShellExecuteW(nullptr, L"open", L"https://github.com/garamond13/w-image-viewer", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::End();
}

void User_interface::dialog_file_open(WIV_OPEN_ file_type)
{
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        return;
    }
    is_dialog_file_open = true;
    SendMessageW(g_hwnd, WIV_WM_SHOW_CURSOR, 0, 0);
    Com_ptr<IFileOpenDialog> file_open_dialog;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(file_open_dialog.put())))) {
        COMDLG_FILTERSPEC filterspec = {};
        filterspec.pszName = L"All supported";
        filterspec.pszSpec = file_type == WIV_OPEN_IMAGE ? WIV_SUPPORTED_EXTENSIONS : L"*.icc" /* WIV_OPEN_ICC */;
        ensure(file_open_dialog->SetFileTypes(1, &filterspec), >= 0);
        if (SUCCEEDED(file_open_dialog->Show(g_hwnd))) {
            Com_ptr<IShellItem> shell_item;
            if (SUCCEEDED(file_open_dialog->GetResult(shell_item.put()))) {
                wchar_t* path;
                if (SUCCEEDED(shell_item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    if (file_type == WIV_OPEN_IMAGE) {
                        if (file_manager.file_open(path)) {
                            ensure(PostMessageW(g_hwnd, WIV_WM_OPEN_FILE, static_cast<WPARAM>(wiv::Open_reason::ExplicitOpen), 0), != 0);
                        }

                    }

                    // WIV_OPEN_ICC
                    else {
                        g_config.cms_display_profile_custom.val = path;
                    }
                    
                    CoTaskMemFree(path);
                }
            }
        }
    }
    CoUninitialize();
    is_dialog_file_open = false;
    on_mouse_activity();
}

void User_interface::toggle_fullscreen()
{
    const bool changed = is_fullscreen() ? fullscreen_.leave(g_hwnd) : fullscreen_.enter(g_hwnd);
    if (changed) {
        on_mouse_activity();
        update_image_orientation();
        if (prenderer_should_update) *prenderer_should_update = true;
    }
}

void User_interface::refresh_monitor(bool follow_window)
{
    if (fullscreen_.transitioning()) return;
    fullscreen_.refresh(g_hwnd, follow_window);
    update_image_orientation();
}

void User_interface::rotate_image(int delta)
{
    rotation_.rotate(delta);
    is_rotating = true;
    if (prenderer_should_update) *prenderer_should_update = true;
}

void User_interface::update_image_orientation(bool reset)
{
    if (!file_manager.image.is_valid()) return;
    const auto monitor = wiv::window_monitor(g_hwnd);
    int angle = 0;
    if (!g_config.image_apply_metadata_orientation.val && g_config.image_align_long_edge.val && monitor.valid()) {
        angle = wiv::long_edge_rotation(file_manager.image.get_width<int>(), file_manager.image.get_height<int>(),
            monitor.info.rcMonitor.right - monitor.info.rcMonitor.left,
            monitor.info.rcMonitor.bottom - monitor.info.rcMonitor.top);
    }
    const int previous_angle = rotation_.angle;
    if (reset) rotation_.reset(angle);
    else rotation_.update(angle);
    if (previous_angle != rotation_.angle) {
        image_pan = ImVec2();
        is_rotating = true;
        if (prenderer_should_update) *prenderer_should_update = true;
    }
}

void User_interface::request_orientation_preferences(bool metadata, bool align)
{
    if (orientation_reload_pending_) return;
    orientation_error_.clear();
    if (metadata != g_config.image_apply_metadata_orientation.val && file_manager.image.is_valid()) {
        if (!file_manager.file_open(file_manager.file_current.c_str(), {metadata, g_config.raw_thumb.val})) {
            orientation_error_ = "Could not reload the current image: " + file_manager.last_error;
            return;
        }
        pending_metadata_ = metadata;
        pending_align_ = align;
        orientation_reload_pending_ = true;
        if (!PostMessageW(g_hwnd, WIV_WM_OPEN_FILE, static_cast<WPARAM>(wiv::Open_reason::SettingsReload), 0)) {
            file_manager.cancel_open();
            finish_settings_reload(false, "Could not schedule the image update.");
        }
        return;
    }
    const bool changed = metadata != g_config.image_apply_metadata_orientation.val
        || align != g_config.image_align_long_edge.val;
    g_config.image_apply_metadata_orientation.val = metadata;
    g_config.image_align_long_edge.val = align;
    if (changed) update_image_orientation(true);
}

void User_interface::finish_settings_reload(bool success, const std::string& error)
{
    if (!orientation_reload_pending_) return;
    if (success) {
        g_config.image_apply_metadata_orientation.val = pending_metadata_;
        g_config.image_align_long_edge.val = pending_align_;
        orientation_error_.clear();
    } else orientation_error_ = "Could not update image orientation: " + error;
    orientation_reload_pending_ = false;
}

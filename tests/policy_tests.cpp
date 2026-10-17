#include "pch.h"
#include "image.h"
#include "file_manager.h"
#include "include/global.h"
#include "include/orientation.h"
#include "include/presentation_policy.h"
#include "include/window_presentation.h"
#include <iostream>
#include <stdexcept>

int beta2_policy_tests(const std::filesystem::path& fixtures, const std::filesystem::path& scratch)
{
    namespace fs = std::filesystem;
    int assertions = 0;
    const auto check = [&](bool condition, const char* message) {
        ++assertions;
        if (!condition) throw std::runtime_error(message);
    };
    using Reason = wiv::Open_reason;
    for (Reason reason : {Reason::Startup, Reason::ExplicitOpen, Reason::Navigation, Reason::Slideshow, Reason::SettingsReload}) {
        check(!wiv::maximize_on_open(reason, false, false), "Disabled maximize policy was ignored");
        check(!wiv::maximize_on_open(reason, true, true), "Maximize policy overrides fullscreen");
        check(wiv::maximize_on_open(reason, true, false) == (reason == Reason::Startup || reason == Reason::ExplicitOpen),
            "Opening reason incorrectly triggers maximization");
    }
    for (const auto image : {SIZE{300, 100}, SIZE{100, 300}, SIZE{100, 100}}) {
        for (const auto monitor : {SIZE{1920, 1080}, SIZE{1080, 1920}, SIZE{1024, 1024}}) {
            const int expected = image.cx == image.cy || monitor.cx == monitor.cy ? 0
                : (image.cx > image.cy) == (monitor.cx > monitor.cy) ? 0 : 90;
            check(wiv::long_edge_rotation(image.cx, image.cy, monitor.cx, monitor.cy) == expected,
                "Monitor long-edge alignment is incorrect");
        }
    }
    wiv::Rotation_state rotation;
    rotation.reset(90); rotation.rotate(90);
    check(rotation.angle == 180 && rotation.manual_override, "First manual turn discarded automatic rotation");
    check(!rotation.update(0) && rotation.angle == 180, "Monitor change overrode manual rotation");
    rotation.reset(90);
    check(rotation.update(0) && rotation.angle == 0 && !rotation.manual_override, "Automatic alignment did not resume");
    const RECT work{-2560, -900, 0, 500};
    RECT bounds = wiv::auto_window_bounds(100, 200, work, {-4000, -2000, -3000, -1000}, 16, 40, false);
    check(bounds.left == work.left && bounds.top == work.top && bounds.right - bounds.left == 116
        && bounds.bottom - bounds.top == 240, "Auto dimensions lost monitor origin or original size");
    bounds = wiv::auto_window_bounds(8000, 4000, work, {}, 16, 40, true);
    check(bounds.left >= work.left && bounds.top >= work.top && bounds.right <= work.right && bounds.bottom <= work.bottom,
        "Auto dimensions exceeds monitor work area");
    check(std::abs((bounds.left - work.left) - (work.right - bounds.right)) <= 1, "Auto dimensions centered on the combined desktop");
    std::cout << "PASS: opening reasons, monitor-based alignment, manual override, signed work-area geometry\n";

    const auto same_pixels = [&](const Image& first, const Image& second) {
        return first.get_width<int>() == second.get_width<int>() && first.get_height<int>() == second.get_height<int>()
            && first.get_basetype() == second.get_basetype() && first.pixel_bytes() == second.pixel_bytes()
            && std::equal(first.pixels(), first.pixels() + first.get_width<size_t>() * first.get_height<size_t>() * first.pixel_bytes(), second.pixels());
    };
    for (const auto* extension : {L"png", L"jpg", L"tiff"}) {
        Image reference;
        check(reference.open(fixtures / (std::wstring(L"orientation-1.") + extension), {false, false}), "Cannot read un-oriented reference");
        for (int orientation = 1; orientation <= 8; ++orientation) {
            Image decoded;
            check(decoded.open(fixtures / (L"orientation-" + std::to_wstring(orientation) + L"." + extension), {false, false}),
                "Cannot read image with EXIF disabled");
            check(same_pixels(reference, decoded), "EXIF-disabled pixels differ from stored raster");
        }
    }
    Image ycbcr_reference;
    check(ycbcr_reference.open(fixtures / "ycbcr-1.tiff", {false, false}), "Cannot open YCbCr reference");
    for (int orientation = 1; orientation <= 8; ++orientation) {
        Image decoded;
        check(decoded.open(fixtures / ("ycbcr-" + std::to_string(orientation) + ".tiff"), {false, false}), "Cannot read YCbCr without EXIF");
        check(same_pixels(decoded, ycbcr_reference), "TIFF fallback still applies orientation when disabled");
        Image native_on, native_off;
        const auto path = scratch / ("jxl-8-" + std::to_string(orientation) + ".jxl");
        check(native_on.open(path, {true, false}) && native_off.open(path, {false, false}), "Cannot read native JXL orientation");
        check(same_pixels(native_on, native_off), "EXIF preference suppressed native JXL orientation");
    }
    for (int orientation : {2, 6, 7, 8}) {
        const auto path = fixtures / ("orientation-" + std::to_string(orientation) + ".avif");
        Image native_on, native_off;
        check(native_on.open(path, {true, false}) && native_off.open(path, {false, false}), "Cannot read native AVIF orientation");
        check(same_pixels(native_on, native_off), "EXIF preference suppressed native AVIF orientation");
    }
    for (bool thumbnail : {false, true}) {
        Image reference;
        const std::string prefix = thumbnail ? "raw-preview-" : "raw-full-";
        check(reference.open(fixtures / (prefix + "1.dng"), {false, thumbnail}), "Cannot read generated DNG reference");
        std::cout << "RAW reference " << prefix << reference.get_width<int>() << 'x' << reference.get_height<int>() << '\n';
        check(reference.get_width<int>() == (thumbnail ? 64 : 128), "Expected RAW thumbnail/full decode path was not selected");
        for (int orientation : {1, 3, 6, 8}) {
            const auto path = fixtures / (prefix + std::to_string(orientation) + ".dng");
            Image disabled, enabled;
            check(disabled.open(path, {false, thumbnail}) && enabled.open(path, {true, thumbnail}), "Cannot decode DNG orientation case");
            check(same_pixels(reference, disabled), "RAW camera rotation remained enabled");
            int width = reference.get_width<int>(), height = reference.get_height<int>();
            std::vector<uint8_t> expected(reference.pixels(), reference.pixels() + size_t(width) * height * reference.pixel_bytes());
            wiv::orient_pixels(expected, width, height, reference.pixel_bytes(), orientation);
            check(enabled.get_width<int>() == width && enabled.get_height<int>() == height, "RAW enabled orientation dimensions are incorrect");
            check(std::equal(expected.begin(), expected.end(), enabled.pixels()), "RAW enabled orientation pixels are incorrect");
        }
    }
    Image broken_preview, full_reference;
    check(broken_preview.open(fixtures / "raw-broken-preview.dng", {false, true})
        && full_reference.open(fixtures / "raw-full-1.dng", {false, false}), "Broken RAW preview did not fall back");
    check(same_pixels(broken_preview, full_reference), "RAW preview fallback changed disabled orientation");
    std::cout << "PASS: EXIF disabled for all eight tags, TIFF fallback, native JXL/AVIF transforms, full RAW and JPEG previews\n";

    File_manager manager;
    const auto current = fixtures / "orientation-6.png";
    check(manager.file_open(current.c_str(), {true, false}), "Cannot stage current image");
    manager.commit_open();
    check(!manager.file_open((scratch / "missing-image.png").c_str(), {false, false}), "Missing reload unexpectedly succeeded");
    check(manager.image.get_width<int>() == 40 && manager.file_current == current, "Failed reload destroyed current presentation");
    check(manager.file_open(current.c_str(), {false, false}), "Cannot stage disabled-orientation reload");
    check(manager.image.get_width<int>() == 40 && manager.pending_image()->get_width<int>() == 64, "Reload was committed before upload");
    manager.cancel_open();
    check(manager.image.get_width<int>() == 40, "Canceled upload destroyed current image");
    check(manager.file_open(current.c_str(), {false, false}), "Cannot restage reload"); manager.commit_open();
    check(manager.image.get_width<int>() == 64 && manager.file_current == current, "Successful reload did not update only the current image");

    const auto config_directory = scratch / "configuration";
    fs::create_directories(config_directory);
    Config config{};
    check(config.window_maximize_on_open.val && config.image_apply_metadata_orientation.val && !config.image_align_long_edge.val,
        "New settings have incorrect defaults");
    config.window_width.val = 817;
    config.window_maximize_on_open.val = false; config.image_apply_metadata_orientation.val = false; config.image_align_long_edge.val = true;
    config.write(config_directory);
    Config loaded{}; loaded.read(config_directory);
    check(!loaded.window_maximize_on_open.val && !loaded.image_apply_metadata_orientation.val && loaded.image_align_long_edge.val,
        "New settings did not survive serialization");
    std::ifstream input(config_directory / "config.dat");
    std::string line, legacy;
    while (std::getline(input, line)) if (line.rfind("wmo=", 0) && line.rfind("imo=", 0) && line.rfind("ile=", 0)) legacy += line + '\n';
    input.close();
    { std::ofstream output(config_directory / "config.dat"); output << '\n' << legacy; }
    loaded.read(config_directory);
    check(loaded.window_maximize_on_open.val && loaded.image_apply_metadata_orientation.val && !loaded.image_align_long_edge.val
        && loaded.window_width.val == 817, "Revert did not reset missing legacy keys to defaults");
    std::cout << "PASS: transactional orientation reload and configuration persistence/legacy-key revert\n";
    return assertions;
}

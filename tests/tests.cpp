#include "pch.h"
#include "image.h"
#include "file_manager.h"
#include "include/orientation.h"
#include "include/navigation_visibility.h"
#include <jxl/encode.h>
#include <jxl/encode_cxx.h>
#include <jxl/color_encoding.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
static int assertions = 0;
#define CHECK(expression) do { ++assertions; if (!(expression)) throw std::runtime_error(std::string("Failed: ") + #expression + " at line " + std::to_string(__LINE__)); } while (0)

std::vector<uint8_t> read_file(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void orientation_tests()
{
    const int expected[8][6] = {{1,2,3,4,5,6},{3,2,1,6,5,4},{6,5,4,3,2,1},{4,5,6,1,2,3},
                               {1,4,2,5,3,6},{4,1,5,2,6,3},{6,3,5,2,4,1},{3,6,2,5,1,4}};
    for (size_t pixel_bytes : {size_t(4), size_t(8), size_t(16)}) {
        for (int orientation = 1; orientation <= 8; ++orientation) {
            int width = 3, height = 2;
            std::vector<uint8_t> pixels(6 * pixel_bytes);
            for (int p = 0; p < 6; ++p)
                for (size_t c = 0; c < pixel_bytes; ++c) pixels[p * pixel_bytes + c] = uint8_t(p + 1);
            wiv::orient_pixels(pixels, width, height, pixel_bytes, orientation);
            CHECK(width == (orientation >= 5 ? 2 : 3));
            CHECK(height == (orientation >= 5 ? 3 : 2));
            for (int p = 0; p < 6; ++p)
                for (size_t c = 0; c < pixel_bytes; ++c) CHECK(pixels[p * pixel_bytes + c] == expected[orientation - 1][p]);
        }
    }
    CHECK(wiv::valid_orientation(0) == 1 && wiv::valid_orientation(9) == 1);
    std::cout << "PASS: eight lossless orientation permutations at 8/16/32-bit storage\n";
}

void visibility_tests()
{
    wiv::Navigation_visibility visibility;
    CHECK(visibility.update(0, false, false, true) == 0);
    CHECK(visibility.update(1, true, false, true) == 1);
    CHECK(visibility.update(2.99, false, false, true) == 1);
    CHECK(std::abs(visibility.update(3.15, false, false, true) - 0.5f) < 0.001f);
    CHECK(visibility.update(3.31, false, false, true) == 0);
    CHECK(visibility.update(4, true, false, true) == 1);
    CHECK(visibility.update(40, false, true, true) == 1);
    CHECK(visibility.update(400, false, true, true) == 1);
    CHECK(visibility.update(401, false, false, true) == 1);
    CHECK(visibility.update(402.99, false, false, true) == 1);
    CHECK(visibility.update(403.31, false, false, true) == 0);
    CHECK(visibility.update(404, true, false, false) == 0);
    CHECK(visibility.update(405, true, false, true) == 1);
    std::cout << "PASS: reveal, inactivity, fade, stationary hover, leave delay, suppression\n";
}

void compare_fixture(const fs::path& path, int width, int height, int tolerance)
{
    Image image;
    CHECK(image.open(path));
    CHECK(image.get_width<int>() == width && image.get_height<int>() == height);
    CHECK(image.get_basetype() == OIIO::TypeDesc::UINT8);
    const auto expected = read_file(fs::path(path.wstring() + L".rgba"));
    CHECK(expected.size() == size_t(width) * height * 4);
    int worst = 0;
    for (size_t i = 0; i < expected.size(); ++i)
        worst = std::max(worst, std::abs(int(expected[i]) - int(image.pixels()[i])));
    if (worst > tolerance) std::cerr << path.filename().string() << " worst pixel error " << worst << '\n';
    CHECK(worst <= tolerance);
    CHECK(image.profile != nullptr);
}

void fixture_tests(const fs::path& fixtures)
{
    g_config.raw_thumb.val = false;
    for (const auto* extension : {L"png", L"jpg", L"tiff"}) {
        for (int orientation = 1; orientation <= 8; ++orientation) {
            const auto path = fixtures / (L"orientation-" + std::to_wstring(orientation) + L"." + extension);
            compare_fixture(path, orientation >= 5 ? 40 : 64, orientation >= 5 ? 64 : 40,
                            std::wstring(extension) == L"jpg" ? 3 : 0);
        }
    }
    compare_fixture(fixtures / "alpha.png", 64, 40, 0);
    compare_fixture(fixtures / "gray-alpha.png", 64, 40, 0);
    compare_fixture(fixtures / "plain.webp", 64, 40, 0);
    compare_fixture(fixtures / "plain.avif", 64, 40, 5);
    compare_fixture(fixtures / "alpha.avif", 64, 40, 5);
    for (int orientation : {2, 6, 7, 8})
        compare_fixture(fixtures / (L"orientation-" + std::to_wstring(orientation) + L".avif"),
                        orientation >= 5 ? 40 : 64, orientation >= 5 ? 64 : 40, 5);
    Image image;
    CHECK(image.open(fixtures / "orientation-6.png"));
    CHECK(image.open(fixtures / "orientation-1.png"));
    CHECK(image.get_width<int>() == 64 && image.get_height<int>() == 40);
    CHECK(!image.open(fixtures / "corrupt.png"));
    CHECK(image.is_valid() && image.get_width<int>() == 64);
    g_config.raw_thumb.val = true;
    CHECK(image.open(fixtures / "orientation-7.png"));
    CHECK(image.get_width<int>() == 40);
    std::cout << "PASS: independent JPEG/PNG/TIFF orientation references, WebP, AVIF, alpha, metadata reset, failed-load preservation\n";
}

void encode_jxl(const fs::path& path, int bits, int orientation, int channels)
{
    auto encoder = JxlEncoderMake(nullptr);
    CHECK(encoder != nullptr);
    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);
    info.xsize = 3; info.ysize = 2; info.bits_per_sample = bits;
    info.num_color_channels = channels <= 2 ? 1 : 3;
    info.num_extra_channels = channels == 2 || channels == 4 ? 1 : 0;
    info.alpha_bits = info.num_extra_channels ? bits : 0;
    info.orientation = static_cast<JxlOrientation>(orientation);
    info.uses_original_profile = JXL_TRUE;
    CHECK(JxlEncoderSetBasicInfo(encoder.get(), &info) == JXL_ENC_SUCCESS);
    JxlColorEncoding color;
    JxlColorEncodingSetToSRGB(&color, info.num_color_channels == 1);
    CHECK(JxlEncoderSetColorEncoding(encoder.get(), &color) == JXL_ENC_SUCCESS);
    auto* settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
    CHECK(JxlEncoderSetFrameLossless(settings, JXL_TRUE) == JXL_ENC_SUCCESS);
    const JxlPixelFormat format{uint32_t(channels), bits <= 8 ? JXL_TYPE_UINT8 : JXL_TYPE_UINT16, JXL_NATIVE_ENDIAN, 0};
    const size_t bytes = bits <= 8 ? 1 : 2;
    std::vector<uint8_t> pixels(6 * channels * bytes);
    for (int p = 0; p < 6; ++p) {
        for (int c = 0; c < channels; ++c) {
            const uint16_t value = (c == channels - 1 && info.num_extra_channels) ? (p % 2 ? 65535 : 32896) : uint16_t((p + 1) * 65535 / 6);
            if (bytes == 1) pixels[p * channels + c] = uint8_t(value / 257);
            else reinterpret_cast<uint16_t*>(pixels.data())[p * channels + c] = value;
        }
    }
    CHECK(JxlEncoderAddImageFrame(settings, &format, pixels.data(), pixels.size()) == JXL_ENC_SUCCESS);
    JxlEncoderCloseInput(encoder.get());
    std::vector<uint8_t> compressed(4096);
    uint8_t* next = compressed.data();
    size_t available = compressed.size();
    CHECK(JxlEncoderProcessOutput(encoder.get(), &next, &available) == JXL_ENC_SUCCESS);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(compressed.data()), compressed.size() - available);
    CHECK(file.good());
}

void jxl_tests(const fs::path& scratch)
{
    const int expected[8][6] = {{1,2,3,4,5,6},{3,2,1,6,5,4},{6,5,4,3,2,1},{4,5,6,1,2,3},
                               {1,4,2,5,3,6},{4,1,5,2,6,3},{6,3,5,2,4,1},{3,6,2,5,1,4}};
    for (int bits : {8, 10, 12, 16}) {
        for (int orientation = 1; orientation <= 8; ++orientation) {
            const auto path = scratch / ("jxl-" + std::to_string(bits) + "-" + std::to_string(orientation) + ".jxl");
            encode_jxl(path, bits, orientation, 3);
            Image image;
            CHECK(image.open(path));
            CHECK(image.get_width<int>() == (orientation >= 5 ? 2 : 3));
            CHECK(image.get_height<int>() == (orientation >= 5 ? 3 : 2));
            CHECK(image.profile != nullptr);
            for (int p = 0; p < 6; ++p) {
                const int actual = bits <= 8 ? image.pixels()[p * 4] : reinterpret_cast<const uint16_t*>(image.pixels())[p * 4];
                const int target = bits <= 8 ? (expected[orientation - 1][p] * 65535 / 6) / 257 : expected[orientation - 1][p] * 65535 / 6;
                CHECK(std::abs(actual - target) <= (bits <= 8 ? 1 : 128));
            }
        }
    }
    for (int channels : {1, 2, 4}) {
        const auto path = scratch / ("jxl-channels-" + std::to_string(channels) + ".jxl");
        encode_jxl(path, 8, 1, channels);
        Image image;
        CHECK(image.open(path));
        CHECK(image.has_alpha() == (channels != 1));
        CHECK(image.pixels()[0] == image.pixels()[1] && image.pixels()[1] == image.pixels()[2]);
        if (channels != 1) { CHECK(image.pixels()[3] == 128); CHECK(std::abs(int(image.pixels()[0]) - 21) <= 1); }
    }
    std::cout << "PASS: JPEG XL 8/10/12/16-bit, all native orientations, grayscale, alpha, ICC profiles\n";
}

void navigation_tests(const fs::path& fixtures, const fs::path& scratch)
{
    const auto directory = scratch / L"mixed-\u00e9";
    fs::create_directories(directory);
    const std::vector<std::pair<std::string, std::string>> copies = {
        {"orientation-1.png", "01.png"}, {"orientation-1.jpg", "02.jpg"}, {"plain.webp", "03.webp"},
        {"plain.avif", "04.avif"}, {"orientation-1.png", "06.UPPER.PNG"}, {"corrupt.png", "07.png"}};
    for (const auto& [from, to] : copies) fs::copy_file(fixtures / from, directory / to, fs::copy_options::overwrite_existing);
    fs::copy_file(scratch / "jxl-8-1.jxl", directory / "05.jxl", fs::copy_options::overwrite_existing);
    File_manager manager;
    CHECK(manager.file_open((directory / "01.png").c_str()));
    CHECK(!manager.image.is_valid());
    manager.commit_open();
    for (const auto* name : {"02.jpg", "03.webp", "04.avif", "05.jxl", "06.UPPER.PNG"}) {
        CHECK(manager.file_next()); manager.commit_open(); CHECK(manager.file_current.filename() == name);
    }
    CHECK(!manager.file_next());
    CHECK(manager.image.is_valid() && manager.file_current.filename() == "06.UPPER.PNG");
    CHECK(!manager.file_open((directory / "07.png").c_str()));
    CHECK(manager.image.is_valid());
    CHECK(manager.file_previous()); manager.commit_open(); CHECK(manager.file_current.filename() == "05.jxl");
    g_config.cycle_files.val = true;
    CHECK(manager.file_open((directory / "06.UPPER.PNG").c_str())); manager.commit_open();
    CHECK(manager.file_next()); manager.commit_open(); CHECK(manager.file_current.filename() == "01.png");
    CHECK(manager.file_previous()); manager.commit_open(); CHECK(manager.file_current.filename() == "06.UPPER.PNG");
    g_config.cycle_files.val = false;
    fs::remove(manager.file_current);
    CHECK(manager.file_previous()); manager.commit_open(); CHECK(manager.file_current.filename() == "05.jxl");
    std::cout << "PASS: mixed PNG/JPEG/WebP/AVIF/JXL navigation, uppercase, Unicode, corrupt neighbors, boundaries, cycling, deleted current file\n";
}

int wmain(int argc, wchar_t* argv[])
{
    try {
        CHECK(argc == 3);
        const fs::path fixtures = fs::absolute(argv[1]), scratch = fs::absolute(argv[2]);
        fs::create_directories(scratch);
        orientation_tests(); visibility_tests(); fixture_tests(fixtures);
        jxl_tests(scratch); navigation_tests(fixtures, scratch);
        std::cout << "PASS: " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

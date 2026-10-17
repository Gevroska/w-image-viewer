#include "pch.h"
#include "image.h"
#include "file_manager.h"
#include "include/global.h"
#include "include/orientation.h"
#include "include/navigation_visibility.h"
#include <jxl/encode.h>
#include <jxl/encode_cxx.h>
#include <jxl/color_encoding.h>
#include <libheif/heif.h>
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
    const auto source_bytes = read_file(path);
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
    CHECK(read_file(path) == source_bytes);
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
    for (int orientation = 1; orientation <= 8; ++orientation)
        compare_fixture(fixtures / (L"ycbcr-" + std::to_wstring(orientation) + L".tiff"),
                        orientation >= 5 ? 40 : 64, orientation >= 5 ? 64 : 40, 4);
    compare_fixture(fixtures / "gray-alpha.png", 64, 40, 0);
    compare_fixture(fixtures / "plain.webp", 64, 40, 0);
    compare_fixture(fixtures / "plain.png", 64, 40, 0);
    compare_fixture(fixtures / "orientation-0.png", 64, 40, 0);
    compare_fixture(fixtures / "orientation-9.png", 64, 40, 0);
    compare_fixture(fixtures / "plain.avif", 64, 40, 5);
    compare_fixture(fixtures / "alpha.avif", 64, 40, 5);
    for (int orientation : {2, 6, 7, 8})
        compare_fixture(fixtures / (L"orientation-" + std::to_wstring(orientation) + L".avif"),
                        orientation >= 5 ? 40 : 64, orientation >= 5 ? 64 : 40, 5);
    Image image;
    CHECK(image.open(fixtures / "gray16-orientation6.png"));
    CHECK(image.get_basetype() == OIIO::TypeDesc::UINT16);
    CHECK(image.get_width<int>() == 2 && image.get_height<int>() == 3);
    const uint16_t expected16[] = {400, 100, 500, 200, 600, 300};
    const auto* pixels16 = reinterpret_cast<const uint16_t*>(image.pixels());
    for (int p = 0; p < 6; ++p) {
        CHECK(pixels16[4 * p] == expected16[p] && pixels16[4 * p + 1] == expected16[p]
            && pixels16[4 * p + 2] == expected16[p] && pixels16[4 * p + 3] == 65535);
    }
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

void encode_jxl(const fs::path& path, int bits, int orientation, int channels, const std::vector<uint8_t>* icc = nullptr)
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
    if (icc) CHECK(JxlEncoderSetICCProfile(encoder.get(), icc->data(), icc->size()) == JXL_ENC_SUCCESS);
    else CHECK(JxlEncoderSetColorEncoding(encoder.get(), &color) == JXL_ENC_SUCCESS);
    auto* settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
    CHECK(JxlEncoderSetFrameLossless(settings, JXL_TRUE) == JXL_ENC_SUCCESS);
    const JxlPixelFormat format{uint32_t(channels), bits <= 8 ? JXL_TYPE_UINT8 : JXL_TYPE_UINT16, JXL_NATIVE_ENDIAN, 0};
    const size_t bytes = bits <= 8 ? 1 : 2;
    std::vector<uint8_t> pixels(6 * channels * bytes);
    for (int p = 0; p < 6; ++p) {
        for (int c = 0; c < channels; ++c) {
            const int color_label = info.num_color_channels == 1 || c == 0 ? p + 1 : c == 1 ? 6 - p : (2 * p) % 6 + 1;
            const uint16_t value = (c == channels - 1 && info.num_extra_channels) ? (p % 2 ? 65535 : 32896) : uint16_t(color_label * 65535 / 6);
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
                const int label = expected[orientation - 1][p];
                const int labels[] = {label, 7 - label, (2 * (label - 1)) % 6 + 1};
                for (int c = 0; c < 3; ++c) {
                    const int actual = bits <= 8 ? image.pixels()[p * 4 + c] : reinterpret_cast<const uint16_t*>(image.pixels())[p * 4 + c];
                    const int target = bits <= 8 ? (labels[c] * 65535 / 6) / 257 : labels[c] * 65535 / 6;
                    CHECK(std::abs(actual - target) <= (bits <= 8 ? 1 : 128));
                }
            }
        }
    }
    for (int channels : {1, 2, 4}) {
        const auto path = scratch / ("jxl-channels-" + std::to_string(channels) + ".jxl");
        encode_jxl(path, 8, 1, channels);
        Image image;
        CHECK(image.open(path));
        CHECK(image.has_alpha() == (channels != 1));
        if (channels <= 2) CHECK(image.pixels()[0] == image.pixels()[1] && image.pixels()[1] == image.pixels()[2]);
        if (channels == 4) CHECK(image.pixels()[1] == 128 && image.pixels()[2] == 21);
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
    CHECK(manager.file_open((directory / "01.PNG").c_str()));
    CHECK(!manager.image.is_valid());
    manager.commit_open();
    CHECK(manager.file_current.filename() == "01.png");
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

void embedded_profile_tests(const fs::path& scratch)
{
    const cmsCIExyYTRIPLE primaries = {{0.64,0.33,1}, {0.3,0.6,1}, {0.15,0.06,1}};
    auto* curve = cmsBuildGamma(nullptr, 1.8);
    CHECK(curve != nullptr);
    cmsToneCurve* curves[] = {curve, curve, curve};
    auto profile = cmsCreateRGBProfile(cmsD50_xyY(), &primaries, curves);
    cmsFreeToneCurve(curve);
    CHECK(profile != nullptr);
    cmsUInt32Number length = 0;
    CHECK(cmsSaveProfileToMem(profile, nullptr, &length));
    std::vector<uint8_t> icc(length);
    CHECK(cmsSaveProfileToMem(profile, icc.data(), &length));
    cmsCloseProfile(profile);
    const auto jxl = scratch / "custom-profile.jxl";
    encode_jxl(jxl, 8, 1, 3, &icc);
    Image image;
    CHECK(image.open(jxl));
    CHECK(image.profile && std::abs(cmsDetectRGBProfileGamma(image.profile.get(), 0.01) - 1.8) < 0.01);

    for (int bits : {8, 10, 12}) {
        auto* context = heif_context_alloc();
        CHECK(context != nullptr);
        heif_encoder* encoder = nullptr;
        CHECK(heif_context_get_encoder_for_format(context, heif_compression_AV1, &encoder).code == heif_error_Ok);
        CHECK(heif_encoder_set_lossy_quality(encoder, 100).code == heif_error_Ok);
        heif_image* source = nullptr;
        const auto chroma = bits == 8 ? heif_chroma_interleaved_RGB : heif_chroma_interleaved_RRGGBB_LE;
        CHECK(heif_image_create(8, 8, heif_colorspace_RGB, chroma, &source).code == heif_error_Ok);
        CHECK(heif_image_add_plane(source, heif_channel_interleaved, 8, 8, bits).code == heif_error_Ok);
        int stride = 0;
        auto* pixels = heif_image_get_plane(source, heif_channel_interleaved, &stride);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8 * 3; ++x) {
                if (bits == 8) pixels[y * stride + x] = 128;
                else reinterpret_cast<uint16_t*>(pixels + y * stride)[x] = uint16_t(((1 << bits) - 1) / 2);
            }
        }
        CHECK(heif_image_set_raw_color_profile(source, "prof", icc.data(), icc.size()).code == heif_error_Ok);
        heif_image_handle* handle = nullptr;
        CHECK(heif_context_encode_image(context, source, encoder, nullptr, &handle).code == heif_error_Ok);
        CHECK(heif_image_handle_get_luma_bits_per_pixel(handle) == bits);
        const auto avif = scratch / ("custom-profile-" + std::to_string(bits) + ".avif");
        CHECK(heif_context_write_to_file(context, avif.string().c_str()).code == heif_error_Ok);
        heif_image_handle_release(handle); heif_image_release(source);
        heif_encoder_release(encoder); heif_context_free(context);
        CHECK(image.open(avif));
        CHECK(image.profile && std::abs(cmsDetectRGBProfileGamma(image.profile.get(), 0.01) - 1.8) < 0.01);
        CHECK(image.get_basetype() == OIIO::TypeDesc::UINT8 && std::abs(int(image.pixels()[0]) - 128) <= 3);
    }
    std::cout << "PASS: custom gamma-1.8 ICC profiles survive JXL and 8/10/12-bit AVIF decoding without sRGB fallback\n";

    auto* context = heif_context_alloc();
    CHECK(context != nullptr);
    heif_encoder* encoder = nullptr;
    CHECK(heif_context_get_encoder_for_format(context, heif_compression_AV1, &encoder).code == heif_error_Ok);
    heif_image* source = nullptr;
    CHECK(heif_image_create(8, 8, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &source).code == heif_error_Ok);
    CHECK(heif_image_add_plane(source, heif_channel_interleaved, 8, 8, 8).code == heif_error_Ok);
    int stride = 0;
    auto* pixels = heif_image_get_plane(source, heif_channel_interleaved, &stride);
    for (int y = 0; y < 8; ++y) std::memset(pixels + y * stride, 128, 24);
    auto* nclx = heif_nclx_color_profile_alloc();
    CHECK(nclx != nullptr);
    CHECK(heif_nclx_color_profile_set_color_primaries(nclx, 12).code == heif_error_Ok); // Display P3
    CHECK(heif_nclx_color_profile_set_transfer_characteristics(nclx, 13).code == heif_error_Ok); // sRGB
    CHECK(heif_nclx_color_profile_set_matrix_coefficients(nclx, 1).code == heif_error_Ok);
    CHECK(heif_image_set_nclx_color_profile(source, nclx).code == heif_error_Ok);
    heif_image_handle* handle = nullptr;
    auto* options = heif_encoding_options_alloc();
    CHECK(options != nullptr);
    options->output_nclx_profile = nclx;
    CHECK(heif_context_encode_image(context, source, encoder, options, &handle).code == heif_error_Ok);
    options->output_nclx_profile = nullptr;
    heif_encoding_options_free(options);
    const auto avif = scratch / "nclx-p3.avif";
    CHECK(heif_context_write_to_file(context, avif.string().c_str()).code == heif_error_Ok);
    heif_image_handle_release(handle); heif_nclx_color_profile_free(nclx);
    heif_image_release(source); heif_encoder_release(encoder); heif_context_free(context);
    CHECK(image.open(avif));
    CHECK(image.profile != nullptr);
    const auto* red = static_cast<const cmsCIEXYZ*>(cmsReadTag(image.profile.get(), cmsSigRedColorantTag));
    CHECK(red && red->X > 0.48); // Display P3's red differs from the sRGB fallback (~0.436).
    std::cout << "PASS: AVIF NCLX Display P3 metadata creates a wide-gamut display profile\n";
}

int wmain(int argc, wchar_t* argv[])
{
    try {
        CHECK(argc == 3);
        const fs::path fixtures = fs::absolute(argv[1]), scratch = fs::absolute(argv[2]);
        fs::create_directories(scratch);
        orientation_tests(); visibility_tests(); fixture_tests(fixtures);
        jxl_tests(scratch); embedded_profile_tests(scratch); navigation_tests(fixtures, scratch);
        std::cout << "PASS: " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#pragma once

#include "pch.h"
#include "include/shader_config.h"

struct Decode_options {
    bool apply_metadata_orientation = true;
    bool raw_thumbnail = true;
};

class Image
{
public:
    bool is_valid() const noexcept { return !pixels_.empty(); }
    bool has_alpha() const noexcept { return alpha_; }
    bool open(const std::filesystem::path& path, Decode_options options = {});
    bool close() noexcept;
    auto get_basetype() const noexcept { return format_.basetype; }
    template<typename T> T get_width() const noexcept { return static_cast<T>(width_); }
    template<typename T> T get_height() const noexcept { return static_cast<T>(height_); }
    template<std::floating_point T> T get_aspect() const noexcept { return get_width<T>() / get_height<T>(); }
    const uint8_t* pixels() const noexcept { return pixels_.data(); }
    size_t pixel_bytes() const noexcept { return 4 * format_.size(); }
    int bitdepth = 0;
    int channels = 0;
    std::string error;
    std::unique_ptr<std::remove_pointer_t<cmsHPROFILE>, decltype(&cmsCloseProfile)> profile = {nullptr, cmsCloseProfile};
    Tone_response_curve trc = {WIV_CMS_TRC_NONE, 0.0f};
private:
    bool decode(const std::filesystem::path& path, Decode_options options);
    bool read_input(OIIO::ImageInput& input, Decode_options options, int fallback_orientation = 1);
    void read_color_profile(const OIIO::ImageSpec& spec);
    std::vector<uint8_t> pixels_;
    int width_ = 0, height_ = 0;
    bool alpha_ = false;
    OIIO::TypeDesc format_ = OIIO::TypeDesc::UINT8;
};

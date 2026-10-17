#include "pch.h"
#include "image.h"
#include "include/global.h"
#include "include/orientation.h"
#include "icc.h"
#include <Imath/half.h>
#include <limits>

namespace {
template<class T> void fill_alpha(std::vector<uint8_t>& pixels)
{
    T opaque;
    if constexpr (std::is_integral_v<T>) opaque = std::numeric_limits<T>::max();
    else opaque = T(1.0f);
    auto* data = reinterpret_cast<T*>(pixels.data());
    for (size_t i = 0; i < pixels.size() / sizeof(T); i += 4) data[i + 3] = opaque;
}

template<class T> void finish_channels(std::vector<uint8_t>& pixels, bool gray, bool associate)
{
    auto* data = reinterpret_cast<T*>(pixels.data());
    for (size_t i = 0; i < pixels.size() / sizeof(T); i += 4) {
        if (gray) data[i + 2] = data[i + 1] = data[i];
        if (associate) {
            if constexpr (std::is_integral_v<T>) {
                for (int c = 0; c < 3; ++c)
                    data[i + c] = static_cast<T>((uint64_t(data[i + c]) * data[i + 3]
                        + std::numeric_limits<T>::max() / 2) / std::numeric_limits<T>::max());
            } else {
                for (int c = 0; c < 3; ++c) data[i + c] = T(float(data[i + c]) * float(data[i + 3]));
            }
        }
    }
}
}

bool Image::open(const std::filesystem::path& path, Decode_options options)
{
    // Decode into a candidate; a bad neighbor must not destroy the current image.
    try {
        Image candidate;
        if (!candidate.decode(path, options)) { error = candidate.error; return false; }
        *this = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool Image::decode(const std::filesystem::path& path, Decode_options options)
{
    OIIO::ImageSpec config;
    config.attribute("bmp:monochrome_detect", 0);
    // Normalize association here, after selecting the display channels.
    config.attribute("oiio:UnassociatedAlpha", 1);
    if (!options.apply_metadata_orientation) config.attribute("raw:user_flip", 0);

    if (options.raw_thumbnail) {
        LibRaw raw;
        if (!options.apply_metadata_orientation) raw.imgdata.params.user_flip = 0;
        if (raw.open_file(path.c_str()) == LIBRAW_SUCCESS && raw.unpack_thumb() == LIBRAW_SUCCESS
            && raw.imgdata.thumbnail.tformat == LIBRAW_THUMBNAIL_JPEG) {
            // Both the RAW owner and its memory reader outlive the complete decode.
            OIIO::Filesystem::IOMemReader reader(raw.imgdata.thumbnail.thumb, raw.imgdata.thumbnail.tlength);
            auto input = OIIO::ImageInput::open("thumbnail.jpg", &config, &reader);
            const int flip = raw.imgdata.sizes.flip;
            const int orientation = flip == 3 ? 3 : flip == 5 ? 8 : flip == 6 ? 6 : 1;
            const bool known_flip = flip == 0 || flip == 3 || flip == 5 || flip == 6;
            if (input && (!options.apply_metadata_orientation || known_flip || input->spec().find_attribute("Orientation"))
                && read_input(*input, options, orientation)) return true;
            // Unsupported bitmap thumbnails and broken JPEG thumbnails use full RAW decoding.
            pixels_.clear();
            profile.reset();
            trc = {WIV_CMS_TRC_NONE, 0.0f};
        }
    }
    const auto utf8 = path.u8string();
    auto input = OIIO::ImageInput::open(std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()), &config);
    if (!input) { error = OIIO::geterror(); return false; }
    return read_input(*input, options);
}

bool Image::read_input(OIIO::ImageInput& input, Decode_options options, int fallback_orientation)
{
    const auto& spec = input.spec();
    constexpr int max_dimension = 16384; // D3D11 2D texture limit.
    if (spec.width <= 0 || spec.height <= 0 || spec.width > max_dimension
        || spec.height > max_dimension || spec.depth > 1 || spec.nchannels < 1) {
        error = "Unsupported image dimensions or channel layout.";
        return false;
    }
    width_ = spec.width;
    height_ = spec.height;
    alpha_ = spec.alpha_channel >= 0 && spec.alpha_channel < spec.nchannels;
    channels = spec.nchannels;
    bitdepth = spec.get_int_attribute("oiio:BitsPerSample", int(spec.format.size() * 8));
    const bool gray = spec.nchannels == 1 || (spec.nchannels == 2 && spec.alpha_channel == 1);
    const int color_channels = gray ? 1 : std::min(3, spec.nchannels);
    if (!gray && color_channels != 3) { error = "Unsupported color channel layout."; return false; }
    switch (spec.format.basetype) {
        case OIIO::TypeDesc::UINT8: format_ = OIIO::TypeDesc::UINT8; break;
        case OIIO::TypeDesc::UINT16: format_ = OIIO::TypeDesc::UINT16; break;
        case OIIO::TypeDesc::HALF: format_ = OIIO::TypeDesc::HALF; break;
        default: format_ = OIIO::TypeDesc::FLOAT; break;
    }
    const size_t count = size_t(width_) * size_t(height_);
    if (count > std::numeric_limits<size_t>::max() / pixel_bytes()) { error = "Image allocation overflow."; return false; }
    pixels_.assign(count * pixel_bytes(), 0);
    switch (format_.basetype) {
        case OIIO::TypeDesc::UINT8: fill_alpha<uint8_t>(pixels_); break;
        case OIIO::TypeDesc::UINT16: fill_alpha<uint16_t>(pixels_); break;
        case OIIO::TypeDesc::HALF: fill_alpha<Imath::half>(pixels_); break;
        default: fill_alpha<float>(pixels_); break;
    }
    const bool adjacent_alpha = alpha_ && spec.alpha_channel == color_channels;
    const int read_channels = color_channels + (adjacent_alpha ? 1 : 0);
    if (!input.read_image(0, 0, 0, read_channels, format_, pixels_.data(), pixel_bytes())
        || (alpha_ && !adjacent_alpha && !input.read_image(0, 0, spec.alpha_channel, spec.alpha_channel + 1, format_,
                                       pixels_.data() + 3 * format_.size(), pixel_bytes()))) {
        error = input.geterror();
        pixels_.clear();
        return false;
    }
    if (gray && adjacent_alpha) {
        for (size_t i = 0; i < count; ++i)
            std::memcpy(pixels_.data() + i * pixel_bytes() + 3 * format_.size(),
                        pixels_.data() + i * pixel_bytes() + format_.size(), format_.size());
    }
    const bool associate = alpha_ && spec.get_int_attribute("oiio:UnassociatedAlpha", 0);
    switch (format_.basetype) {
        case OIIO::TypeDesc::UINT8: finish_channels<uint8_t>(pixels_, gray, associate); break;
        case OIIO::TypeDesc::UINT16: finish_channels<uint16_t>(pixels_, gray, associate); break;
        case OIIO::TypeDesc::HALF: finish_channels<Imath::half>(pixels_, gray, associate); break;
        default: finish_channels<float>(pixels_, gray, associate); break;
    }
    int orientation = spec.get_int_attribute("Orientation", fallback_orientation);
    if (!options.apply_metadata_orientation) orientation = 1;
    // These decoders own their codestream/container/RAW transforms. EXIF copies are not authoritative.
    const std::string_view decoder = input.format_name();
    if (decoder == "jpegxl" || decoder == "heif" || decoder == "raw") orientation = 1;
    wiv::orient_pixels(pixels_, width_, height_, pixel_bytes(), orientation);
    read_color_profile(spec);
    error.clear();
    return true;
}

bool Image::close() noexcept
{
    const bool valid = is_valid();
    pixels_.clear();
    width_ = height_ = 0;
    alpha_ = false;
    profile.reset();
    trc = {WIV_CMS_TRC_NONE, 0.0f};
    return valid;
}

void Image::read_color_profile(const OIIO::ImageSpec& spec)
{
    // ICC metadata owns its exact length; never pass a guessed allocation size to LCMS.
    if (const auto* icc = spec.find_attribute("ICCProfile")) {
        const auto size = icc->datasize();
        if (size > 0 && size <= std::numeric_limits<cmsUInt32Number>::max())
            profile.reset(cmsOpenProfileFromMem(icc->data(), static_cast<cmsUInt32Number>(size)));
        if (profile && cmsGetColorSpace(profile.get()) == cmsSigGrayData) {
            auto* curve = static_cast<cmsToneCurve*>(cmsReadTag(profile.get(), cmsSigGrayTRCTag));
            if (curve) {
                cmsCIExyY white = *cmsD50_xyY();
                const cmsCIExyYTRIPLE primaries = {{0.64, 0.33, 1}, {0.30, 0.60, 1}, {0.15, 0.06, 1}};
                cmsToneCurve* curves[] = {curve, curve, curve};
                auto rgb = cmsCreateRGBProfile(&white, &primaries, curves);
                profile.reset(rgb);
            }
        }
        if (profile && cmsGetColorSpace(profile.get()) == cmsSigRgbData) {
            const float gamma = static_cast<float>(cmsDetectRGBProfileGamma(profile.get(), 0.1));
            trc = gamma > 0.0f ? Tone_response_curve{WIV_CMS_TRC_GAMMA, gamma}
                              : Tone_response_curve{WIV_CMS_TRC_NONE, 0.0f};
            return;
        }
        profile.reset();
    }
    float xy[8]{};
    const int transfer = spec.get_int_attribute("heif:TransferCharacteristics", -1);
    if (transfer >= 0 && transfer != 16 && transfer != 18
        && spec.getattribute("heif:Primaries", OIIO::TypeDesc(OIIO::TypeDesc::FLOAT, 8), xy)
        && xy[1] > 0 && xy[3] > 0 && xy[5] > 0 && xy[7] > 0) {
        const cmsCIExyY white = {xy[0], xy[1], 1};
        const cmsCIExyYTRIPLE primaries = {{xy[2], xy[3], 1}, {xy[4], xy[5], 1}, {xy[6], xy[7], 1}};
        cmsToneCurve* curve;
        if (transfer == 13) {
            const double parameters[] = {2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045};
            curve = cmsBuildParametricToneCurve(nullptr, 4, parameters);
        } else if (transfer == 1 || transfer == 6 || transfer == 14 || transfer == 15) {
            const double parameters[] = {1.0 / 0.45, 1.0 / 1.099, 0.099 / 1.099, 1.0 / 4.5, 0.081};
            curve = cmsBuildParametricToneCurve(nullptr, 4, parameters);
        } else curve = cmsBuildGamma(nullptr, transfer == 8 ? 1.0 : transfer == 5 ? 2.8 : 2.2);
        if (curve) {
            cmsToneCurve* curves[] = {curve, curve, curve};
            profile.reset(cmsCreateRGBProfile(&white, &primaries, curves));
            cmsFreeToneCurve(curve);
            if (profile) { trc = {WIV_CMS_TRC_NONE, 0.0f}; return; }
        }
    }
    const std::string tag(spec.get_string_attribute("oiio:ColorSpace"));
    if (tag.find("sRGB") != std::string::npos || tag == "srgb_rec709_scene") {
        profile.reset(cmsCreate_sRGBProfile());
        trc = {WIV_CMS_TRC_SRGB, 0.0f};
    } else if (tag.find("AdobeRGB") != std::string::npos) {
        profile.reset(cms_create_profile_adobe_rgb());
        trc = {WIV_CMS_TRC_GAMMA, ADOBE_RGB_GAMMA<float>};
    } else if (tag.find("inear") != std::string::npos || tag == "ACEScg" || tag == "lin_srgb") {
        profile.reset((tag == "ACEScg" || (tag != "lin_srgb" && g_config.cms_default_to_aces.val))
                          ? cms_create_profile_aces_cg() : cms_create_profile_linear_srgb());
        trc = {WIV_CMS_TRC_LINEAR, 0.0f};
    } else if (g_config.cms_default_to_srgb.val) {
        profile.reset(cmsCreate_sRGBProfile());
        trc = {WIV_CMS_TRC_SRGB, 0.0f};
    }
}

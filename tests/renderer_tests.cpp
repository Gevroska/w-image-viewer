#include "pch.h"
#include "renderer.h"
#include "window.h"
#include "include/global.h"
#include "include/info.h"
#include "../w-image-viewer/vs_quad_hlsl.h"
#include "../w-image-viewer/ps_sample_alpha_hlsl.h"
#include <iostream>
#include <stdexcept>

struct Renderer_test_access {
    static void initialize(Renderer& renderer)
    {
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, renderer.device.put(), nullptr, renderer.ctx.put())))
            throw std::runtime_error("Could not create WARP graphics device");
        renderer.cms_profile_display.reset(cmsCreate_sRGBProfile());
        renderer.trc = {WIV_CMS_TRC_SRGB, 0.0f};
        renderer.ui.create(renderer.device.get(), renderer.ctx.get(), &renderer.should_update);
    }
    static ID3D11Device* device(Renderer& renderer) { return renderer.device.get(); }
    static ID3D11DeviceContext* context(Renderer& renderer) { return renderer.ctx.get(); }
    static ID3D11ShaderResourceView* image_view(Renderer& renderer) { return renderer.srv_image.get(); }
    static Tone_response_curve curve(Renderer& renderer) { return renderer.trc; }
    static void fail_upload(Renderer& renderer, int stage) { renderer.upload_failure_stage_ = stage; }
};

struct User_interface_test_access {
    static void orientation(User_interface& ui, bool metadata, bool align) { ui.request_orientation_preferences(metadata, align); }
    static bool pending(User_interface& ui) { return ui.orientation_reload_pending_; }
    static bool has_error(User_interface& ui) { return !ui.orientation_error_.empty(); }
};

namespace {
struct Test_host { Renderer* renderer; int completions = 0; };
LRESULT CALLBACK test_window(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE)
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
    auto* host = reinterpret_cast<Test_host*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WIV_WM_OPEN_FILE && host) {
        ++host->completions;
        const bool success = host->renderer->create_image();
        if (static_cast<wiv::Open_reason>(wparam) == wiv::Open_reason::SettingsReload)
            host->renderer->ui.finish_settings_reload(success, success ? "" : "Injected graphics upload failure");
        if (success) {
            host->renderer->ui.reset_image_panzoom();
            host->renderer->ui.update_image_orientation(true);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}

int renderer_regression_tests(const std::filesystem::path& fixtures, const std::filesystem::path& scratch)
{
    int assertions = 0;
    const auto check = [&](bool condition, const char* message) {
        ++assertions;
        if (!condition) throw std::runtime_error(message);
    };
    struct Restore_configuration {
        Config config = g_config;
        HWND window = g_hwnd;
        ~Restore_configuration() { g_config = std::move(config); g_hwnd = window; }
    } restore;
    g_config = Config{};
    g_config.cms_use.val = true;
    g_config.raw_thumb.val = false;
    WNDCLASSW type{};
    type.lpfnWndProc = test_window;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"WivRendererTests";
    check(RegisterClassW(&type) != 0, "Could not register renderer test window");
    struct Unregister { HINSTANCE instance; ~Unregister() { UnregisterClassW(L"WivRendererTests", instance); } } unregister{type.hInstance};
    Renderer renderer;
    Test_host host{&renderer};
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName,
        L"W Image Viewer graphics test", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480,
        nullptr, nullptr, type.hInstance, &host);
    check(g_hwnd != nullptr, "Could not create renderer test window");
    struct Destroy { HWND window; ~Destroy() { DestroyWindow(window); } } destroy{g_hwnd};
    Renderer_test_access::initialize(renderer);
    auto& manager = renderer.ui.file_manager;
    const auto original = fixtures / "orientation-6.png";
    check(manager.file_open(original.c_str()) && renderer.create_image(), "Cannot upload initial image and CMS LUT");
    auto* context = Renderer_test_access::context(renderer);
    Com_ptr<ID3D11ShaderResourceView> original_color;
    context->PSGetShaderResources(2, 1, original_color.put());
    check(bool(original_color), "Initial color LUT was not bound");
    auto* original_view = Renderer_test_access::image_view(renderer);
    const auto* original_pixels = manager.image.pixels();
    renderer.ui.image_pan = {23, -11};
    renderer.ui.image_zoom = 0.25f;
    renderer.ui.rotate_image(90);
    for (int stage = 1; stage <= 4; ++stage) {
        Renderer_test_access::fail_upload(renderer, stage);
        const int before = host.completions;
        User_interface_test_access::orientation(renderer.ui, false, true);
        check(host.completions == before + 1, "Orientation upload was deferred and can be replaced by another open");
        check(!User_interface_test_access::pending(renderer.ui) && !manager.pending_image(), "Failed upload left a pending transaction");
        check(User_interface_test_access::has_error(renderer.ui), "Failed settings upload has no error");
        check(g_config.image_apply_metadata_orientation.val && !g_config.image_align_long_edge.val,
            "Failed upload committed orientation preferences");
        check(manager.file_current == original && manager.image.pixels() == original_pixels,
            "Failed upload replaced the current decoded image or path");
        check(Renderer_test_access::image_view(renderer) == original_view,
            "Failed upload replaced the current GPU image");
        Com_ptr<ID3D11ShaderResourceView> color;
        context->PSGetShaderResources(2, 1, color.put());
        check(color == original_color, "Failed upload replaced the current GPU color transform");
        check(Info::image_width == 40 && Info::image_height == 64, "Failed upload changed displayed image information");
        check(renderer.ui.image_pan.x == 23 && renderer.ui.image_pan.y == -11 && renderer.ui.image_zoom == 0.25f
            && renderer.ui.image_rotation() == 90, "Failed upload reset pan, zoom, or manual rotation");
    }
    Renderer_test_access::fail_upload(renderer, 0);
    User_interface_test_access::orientation(renderer.ui, false, true);
    check(!g_config.image_apply_metadata_orientation.val && g_config.image_align_long_edge.val
        && manager.image.get_width<int>() == 64 && manager.image.get_height<int>() == 40,
        "Successful orientation upload did not commit image and settings together");
    check(!User_interface_test_access::has_error(renderer.ui), "Successful reload retained its previous error");
    check(manager.file_open((scratch / "custom-profile.jxl").c_str()) && renderer.create_image(), "Cannot upload custom-profile image");
    const auto curve = Renderer_test_access::curve(renderer);
    check(curve.id == WIV_CMS_TRC_GAMMA && std::abs(curve.val - 1.8f) < 0.01f,
        "Resampling uses the display transfer curve instead of the image transfer curve");
    std::cout << "PASS: WARP image/LUT texture and view failures preserve image, graphics, view, and preferences; synchronous completion; source transfer curve\n";

    auto* device = Renderer_test_access::device(renderer);
    Com_ptr<ID3D11VertexShader> vertex;
    Com_ptr<ID3D11PixelShader> pixel;
    check(SUCCEEDED(device->CreateVertexShader(VS_QUAD, sizeof(VS_QUAD), nullptr, vertex.put())), "Cannot create production vertex shader");
    check(SUCCEEDED(device->CreatePixelShader(PS_SAMPLE_ALPHA, sizeof(PS_SAMPLE_ALPHA), nullptr, pixel.put())), "Cannot create production transparency shader");
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex.get(), nullptr, 0);
    context->PSSetShader(pixel.get(), nullptr, 0);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const uint32_t transparent = 0;
    D3D11_SUBRESOURCE_DATA data{&transparent, 4, 0};
    Com_ptr<ID3D11Texture2D> texture;
    Com_ptr<ID3D11ShaderResourceView> source;
    check(SUCCEEDED(device->CreateTexture2D(&desc, &data, texture.put())), "Cannot create transparent texture");
    check(SUCCEEDED(device->CreateShaderResourceView(texture.get(), nullptr, source.put())), "Cannot create transparent image view");
    context->PSSetShaderResources(0, 1, &source);
    CD3D11_SAMPLER_DESC sampler_desc(D3D11_DEFAULT);
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    Com_ptr<ID3D11SamplerState> sampler;
    check(SUCCEEDED(device->CreateSamplerState(&sampler_desc, sampler.put())), "Cannot create shader sampler");
    context->PSSetSamplers(1, 1, &sampler);
    desc.Width = 32; desc.Height = 16;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    Com_ptr<ID3D11Texture2D> target;
    Com_ptr<ID3D11RenderTargetView> target_view;
    check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, target.put())), "Cannot create shader target");
    check(SUCCEEDED(device->CreateRenderTargetView(target.get(), nullptr, target_view.put())), "Cannot create shader target view");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Com_ptr<ID3D11Texture2D> readback;
    check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, readback.put())), "Cannot create shader readback texture");
    const D3D11_VIEWPORT viewport{0, 0, 32, 16, 0, 1};
    context->RSSetViewports(1, &viewport);
    for (int angle : {0, 90, 180, 270}) {
        struct alignas(16) Parameters {
            float width = 4, height = 2, angle;
            int rotate;
            float first[4] = {1, 1, 1, 0}, second[4] = {0, 0, 0, 0};
        } parameters{4, 2, float(angle), angle != 0};
        D3D11_BUFFER_DESC buffer_desc{};
        buffer_desc.ByteWidth = sizeof(parameters);
        buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
        buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const D3D11_SUBRESOURCE_DATA buffer_data{&parameters, 0, 0};
        Com_ptr<ID3D11Buffer> buffer;
        check(SUCCEEDED(device->CreateBuffer(&buffer_desc, &buffer_data, buffer.put())), "Cannot create transparency parameters");
        context->PSSetConstantBuffers(0, 1, &buffer);
        context->OMSetRenderTargets(1, &target_view, nullptr);
        context->Draw(3, 0);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(readback.get(), target.get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(SUCCEEDED(context->Map(readback.get(), 0, D3D11_MAP_READ, 0, &mapped)), "Cannot read transparency shader output");
        bool correct = true;
        for (int y = 0; y < 16; ++y) {
            const auto* row = static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch;
            for (int x = 0; x < 32; ++x) {
                const int expected = ((x / 8 + y / 8) % 2) ? 0 : 255;
                correct &= row[4 * x] == expected && row[4 * x + 1] == expected && row[4 * x + 2] == expected && row[4 * x + 3] == 255;
            }
        }
        context->Unmap(readback.get(), 0);
        check(correct, "Rotation stretched or moved the transparency checkerboard");
    }
    std::cout << "PASS: production transparency shader keeps square 8-pixel tiles through all quarter turns\n";
    return assertions;
}

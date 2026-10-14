#include "post_process.h"
#include "test_support.h"
#include <vector>
#include <cmath>

namespace {
constexpr UINT imageSize = 64;
struct DeviceFixture {
    HWND window = nullptr;
    IDirect3D9* direct3d = nullptr;
    IDirect3DDevice9* device = nullptr;
    ~DeviceFixture() {
        post_fx::release(device);
        post_fx::release(direct3d);
        if (window) DestroyWindow(window);
    }
    bool initialize() {
        window = CreateWindowExW(0, L"STATIC", L"Post FX shader test", WS_OVERLAPPED,
            0, 0, imageSize, imageSize, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        direct3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!window || !direct3d) return false;
        D3DPRESENT_PARAMETERS presentation{};
        presentation.Windowed = TRUE;
        presentation.hDeviceWindow = window;
        presentation.SwapEffect = D3DSWAPEFFECT_DISCARD;
        presentation.BackBufferWidth = presentation.BackBufferHeight = imageSize;
        presentation.BackBufferFormat = D3DFMT_UNKNOWN;
        return SUCCEEDED(direct3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &presentation, &device));
    }
};
struct Image {
    explicit Image(UINT size = imageSize) : width(size), height(size) {}
    IDirect3DTexture9* texture = nullptr;
    UINT width, height;
    ~Image() { post_fx::release(texture); }
    bool upload(IDirect3DDevice9* device, const std::vector<DWORD>& pixels) {
        IDirect3DTexture9* staging = nullptr;
        if (FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_SYSTEMMEM, &staging, nullptr))) return false;
        D3DLOCKED_RECT locked{};
        bool success = SUCCEEDED(staging->LockRect(0, &locked, nullptr, 0));
        if (success) {
            for (UINT row = 0; row < height; ++row)
                memcpy(static_cast<unsigned char*>(locked.pBits) + row * locked.Pitch,
                    pixels.data() + row * width, width * sizeof(DWORD));
            staging->UnlockRect(0);
        }
        if (!texture) success = success && SUCCEEDED(device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET,
            D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr));
        success = success && SUCCEEDED(device->UpdateTexture(staging, texture));
        post_fx::release(staging);
        return success;
    }
    bool read(IDirect3DDevice9* device, std::vector<DWORD>& pixels) {
        IDirect3DSurface9* source = nullptr;
        IDirect3DSurface9* staging = nullptr;
        bool success = SUCCEEDED(texture->GetSurfaceLevel(0, &source)) &&
            SUCCEEDED(device->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8,
                D3DPOOL_SYSTEMMEM, &staging, nullptr)) && SUCCEEDED(device->GetRenderTargetData(source, staging));
        D3DLOCKED_RECT locked{};
        success = success && SUCCEEDED(staging->LockRect(&locked, nullptr, D3DLOCK_READONLY));
        if (success) {
            pixels.resize(width * height);
            for (UINT row = 0; row < height; ++row)
                memcpy(pixels.data() + row * width, static_cast<unsigned char*>(locked.pBits) + row * locked.Pitch,
                    width * sizeof(DWORD));
            staging->UnlockRect();
        }
        post_fx::release(source); post_fx::release(staging);
        return success;
    }
};
bool drawPolygon(IDirect3DDevice9* device, Image& image) {
    post_fx::DeviceState saved(device);
    D3DCAPS9 caps{};
    if (FAILED(device->GetDeviceCaps(&caps)) || !saved.capture(caps.NumSimultaneousRTs)) return false;
    IDirect3DSurface9* surface = nullptr;
    if (FAILED(image.texture->GetSurfaceLevel(0, &surface))) return false;
    bool success = SUCCEEDED(device->BeginScene());
    if (success) {
        auto draw = [&]() -> HRESULT {
            HRESULT result = device->SetDepthStencilSurface(nullptr);
            if (FAILED(result)) return result;
            for (UINT index = 1; index < caps.NumSimultaneousRTs; ++index) {
                result = device->SetRenderTarget(index, nullptr);
                if (FAILED(result)) return result;
            }
            result = device->SetTexture(0, nullptr);
            if (FAILED(result)) return result;
            result = device->SetRenderTarget(0, surface);
            if (FAILED(result)) return result;
            const D3DVIEWPORT9 viewport{0, 0, image.width, image.height, 0, 1};
            result = device->SetViewport(&viewport);
            if (FAILED(result)) return result;
            result = device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff181818, 1, 0);
            if (FAILED(result)) return result;
            for (const auto& state : post_fx::passStates) {
                result = device->SetRenderState(state.state, state.value);
                if (FAILED(result)) return result;
            }
            result = device->SetVertexShader(nullptr);
            if (FAILED(result)) return result;
            result = device->SetPixelShader(nullptr);
            if (FAILED(result)) return result;
            result = device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            if (FAILED(result)) return result;
            result = device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            if (FAILED(result)) return result;
            result = device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            if (FAILED(result)) return result;
            result = device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
            if (FAILED(result)) return result;
            result = device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
            if (FAILED(result)) return result;
            struct Vertex { float x, y, z, rhw; DWORD color; };
            const float scale = image.width / 128.0f;
            const Vertex vertices[] = {{8 * scale, 8 * scale, 0, 1, 0xffe0e0e0},
                {112 * scale, 24 * scale, 0, 1, 0xffe0e0e0}, {16 * scale, 118 * scale, 0, 1, 0xffe0e0e0}};
            return device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, sizeof(Vertex));
        };
        success = SUCCEEDED(draw());
        success = SUCCEEDED(device->EndScene()) && success;
    }
    post_fx::release(surface);
    return saved.restore() && success;
}
unsigned difference(const std::vector<DWORD>& left, const std::vector<DWORD>& right) {
    unsigned changed = 0;
    // Clamp filtering at the image border must not count as fixing its interior.
    for (UINT y = 2; y < imageSize - 2; ++y)
        for (UINT x = 2; x < imageSize - 2; ++x) {
            const UINT index = y * imageSize + x;
            if ((left[index] & 0xffffff) != (right[index] & 0xffffff)) ++changed;
        }
    return changed;
}
bool render(post_fx::PostProcess& effects, IDirect3DDevice9* device, Image& image, const post_fx::Settings& settings,
    unsigned factor = 1, IDirect3DTexture9* depth = nullptr) {
    if (FAILED(device->BeginScene())) return false;
    const HRESULT result = effects.render(device, image.texture, depth, {}, 1, settings, factor);
    device->EndScene();
    return SUCCEEDED(result);
}
// distanceAt(x, y) gives the view distance of each pixel, encoded like the game's depth target.
template<class DistanceAt>
bool uploadDepth(IDirect3DDevice9* device, IDirect3DTexture9** texture, DistanceAt distanceAt) {
    IDirect3DTexture9* staging = nullptr;
    if (FAILED(device->CreateTexture(imageSize, imageSize, 1, 0, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &staging, nullptr))) return false;
    constexpr float nearPlane = 0.5f, farPlane = 100;
    const auto encode = [&](float distance) {
        return 0.5f - 0.5f * (farPlane / (farPlane - nearPlane) - nearPlane * farPlane / ((farPlane - nearPlane) * distance));
    };
    D3DLOCKED_RECT locked{};
    bool success = SUCCEEDED(staging->LockRect(0, &locked, nullptr, 0));
    if (success) {
        for (UINT y = 0; y < imageSize; ++y) {
            auto* row = reinterpret_cast<float*>(static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch);
            for (UINT x = 0; x < imageSize; ++x) row[x] = encode(distanceAt(x, y));
        }
        staging->UnlockRect(0);
    }
    if (!*texture) success = success && SUCCEEDED(device->CreateTexture(imageSize, imageSize, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_R32F, D3DPOOL_DEFAULT, texture, nullptr));
    success = success && SUCCEEDED(device->UpdateTexture(staging, *texture));
    post_fx::release(staging);
    return success;
}
post_fx::CameraSample camera(IDirect3DTexture9* depth, float rotation) {
    post_fx::CameraSample result;
    const float cosine = std::cos(rotation), sine = std::sin(rotation);
    result.view.elements[0][0] = cosine; result.view.elements[0][2] = -sine;
    result.view.elements[1][1] = 1;
    result.view.elements[2][0] = sine; result.view.elements[2][2] = cosine;
    result.view.elements[3][3] = 1;
    result.projection.elements[0][0] = result.projection.elements[1][1] = 1;
    result.projection.elements[2][2] = 100.0f / 99.5f;
    result.projection.elements[2][3] = 1;
    result.projection.elements[3][2] = -50.0f / 99.5f;
    result.valid = true; result.camera = result.scene = 1;
    result.depth = reinterpret_cast<uintptr_t>(depth);
    return result;
}
bool renderBlur(post_fx::PostProcess& effects, IDirect3DDevice9* device, Image& image, IDirect3DTexture9* depth,
    const post_fx::Settings& settings, float rotation, double seconds) {
    if (FAILED(device->BeginScene())) return false;
    const HRESULT result = effects.render(device, image.texture, depth, camera(depth, rotation), seconds, settings);
    device->EndScene();
    return SUCCEEDED(result);
}
double imageChange(const std::vector<DWORD>& input, const std::vector<DWORD>& output) {
    double total = 0;
    for (UINT y = 8; y < imageSize - 8; ++y)
        for (UINT x = 8; x < imageSize - 8; ++x) {
            const UINT index = y * imageSize + x;
            total += std::abs(static_cast<int>(input[index] & 0xff) - static_cast<int>(output[index] & 0xff));
        }
    return total / ((imageSize - 16) * (imageSize - 16));
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    DeviceFixture fixture;
    if (!fixture.initialize()) {
        puts("SKIP: A standalone D3D9 HAL device is unavailable. No game was started.");
        return 77;
    }
    const HMODULE module = LoadLibraryA(argv[1]);
    if (!module) return 3;
    post_fx::PostProcess effects;
    test::check("Embedded shaders load", effects.initialize(module));
    Image image;
    post_fx::Settings settings;
    settings.change(post_fx::antialiasing, 1);
    std::vector<DWORD> input(imageSize * imageSize), output;
    for (const char* pattern : {"diagonal", "thin vertical", "checkerboard", "flat", "low contrast", "very low contrast"}) {
        for (UINT y = 0; y < imageSize; ++y)
            for (UINT x = 0; x < imageSize; ++x) {
                bool bright = false;
                if (!strcmp(pattern, "diagonal") || !strcmp(pattern, "low contrast") ||
                    !strcmp(pattern, "very low contrast")) bright = x > 12 + y / 2;
                if (!strcmp(pattern, "thin vertical")) bright = x == 32;
                if (!strcmp(pattern, "checkerboard")) bright = (x + y) % 2 != 0;
                const DWORD intensity = !strcmp(pattern, "flat") ? 96 :
                    !strcmp(pattern, "low contrast") ? (bright ? 132 : 108) :
                    !strcmp(pattern, "very low contrast") ? (bright ? 126 : 114) : (bright ? 224 : 24);
                input[y * imageSize + x] = 0x7f000000 | intensity * 0x010101;
            }
        test::check("Synthetic input uploads", image.upload(fixture.device, input));
        test::check("FXAA draws successfully on a real D3D9 device", render(effects, fixture.device, image, settings));
        test::check("Synthetic output reads back", image.read(fixture.device, output));
        if (output.size() != input.size()) return 4;
        const unsigned changed = difference(input, output);
        printf("  Pattern: %s; changed pixels: %u\n", pattern, changed);
        test::check(!strcmp(pattern, "flat") ? "Flat regions stay unchanged" : "Aliased edges/subpixel detail are filtered",
            !strcmp(pattern, "flat") ? changed == 0 : changed > 0);
        test::check("Alpha is preserved", [&]() {
            for (DWORD pixel : output) if ((pixel >> 24) != 0x7f) return false;
            return true;
        }());
        if (!strcmp(pattern, "thin vertical")) {
            const unsigned center = output[32 * imageSize + 32] & 0xff;
            const unsigned adjacent = output[32 * imageSize + 31] & 0xff;
            printf("  Thin-line center: %u; adjacent pixel: %u\n", center, adjacent);
            test::check("Thin-line contrast is reduced while the line remains visible", center >= 150 && center <= 170);
            test::check("Thin-line coverage extends into adjacent pixels", adjacent > 24);
        }
    }
    settings.change(post_fx::antialiasing, post_fx::aaSmaa);
    for (const char* pattern : {"diagonal", "flat", "color edge"}) {
        for (UINT y = 0; y < imageSize; ++y)
            for (UINT x = 0; x < imageSize; ++x) {
                const bool bright = x > 12 + y / 2;
                DWORD color = !strcmp(pattern, "flat") ? 0x606060 : bright ? 0xe0e0e0 : 0x181818;
                if (!strcmp(pattern, "color edge")) color = bright ? 0xc00000 : 0x003900;
                input[y * imageSize + x] = ((x * 3 + y) % 256 << 24) | color;
            }
        test::check("SMAA synthetic input uploads", image.upload(fixture.device, input));
        test::check("SMAA three-pass pipeline draws on real D3D9", render(effects, fixture.device, image, settings));
        test::check("SMAA output reads back", image.read(fixture.device, output));
        if (output.size() != input.size()) return 4;
        const unsigned changed = difference(input, output);
        printf("  SMAA pattern: %s; changed pixels: %u\n", pattern, changed);
        test::check("SMAA smooths geometric and color edges while keeping flat RGB unchanged",
            !strcmp(pattern, "flat") ? changed == 0 : changed > 0);
        test::check("SMAA preserves per-pixel alpha", [&]() {
            for (UINT index = 0; index < input.size(); ++index)
                if ((input[index] >> 24) != (output[index] >> 24)) return false;
            return true;
        }());
    }
    // A silhouette whose colors differ by 8/255: below the Ultra threshold (0.05), above the
    // predicated one (0.025). Only a depth step along the same line should make it an edge.
    const auto inFront = [](UINT x, UINT y) { return x > 12 + y / 2; };
    for (UINT y = 0; y < imageSize; ++y)
        for (UINT x = 0; x < imageSize; ++x)
            input[y * imageSize + x] = 0x7f000000 | (inFront(x, y) ? 122u : 114u) * 0x010101;
    IDirect3DTexture9* silhouetteDepth = nullptr;
    test::check("Silhouette depth uploads", uploadDepth(fixture.device, &silhouetteDepth,
        [&](UINT x, UINT y) { return inFront(x, y) ? 5.0f : 40.0f; }));
    test::check("Low-contrast silhouette uploads", image.upload(fixture.device, input));
    test::check("SMAA without depth draws the low-contrast silhouette", render(effects, fixture.device, image, settings));
    test::check("That output reads back", image.read(fixture.device, output));
    const unsigned changedWithoutDepth = output.size() == input.size() ? difference(input, output) : 0;
    test::check("Low-contrast silhouette uploads again", image.upload(fixture.device, input));
    test::check("SMAA with a depth step draws the same silhouette",
        render(effects, fixture.device, image, settings, 1, silhouetteDepth));
    test::check("That output reads back too", image.read(fixture.device, output));
    const unsigned changedWithDepth = output.size() == input.size() ? difference(input, output) : 0;
    printf("  SMAA low-contrast silhouette changed pixels: without depth=%u; with depth=%u\n",
        changedWithoutDepth, changedWithDepth);
    test::check("Depth predication smooths a silhouette that color alone leaves aliased",
        changedWithoutDepth == 0 && changedWithDepth > 0);
    for (UINT y = 0; y < imageSize; ++y)
        for (UINT x = 0; x < imageSize; ++x)
            input[y * imageSize + x] = 0x7f000000 | ((x + y) % 7 ? 96u : 100u) * 0x010101;
    test::check("Faint texture uploads", image.upload(fixture.device, input));
    test::check("SMAA with flat depth draws the faint texture",
        render(effects, fixture.device, image, settings, 1, silhouetteDepth));
    test::check("Faint texture output reads back", image.read(fixture.device, output));
    test::check("A faint texture away from the depth step stays untouched", [&]() {
        for (UINT y = 4; y < imageSize - 4; ++y)
            for (UINT x = 12 + y / 2 + 4; x < imageSize - 4; ++x)
                if (output[y * imageSize + x] != input[y * imageSize + x]) return false;
        return true;
    }());
    post_fx::release(silhouetteDepth);

    settings.change(post_fx::antialiasing, post_fx::aaOff);
    // A soft vertical edge: dark until x = 28, a 6-pixel ramp, then bright.
    const auto softEdge = [](UINT x) { return x < 28 ? 64u : x >= 34 ? 192u : 64u + (x - 28) * 128u / 6; };
    for (UINT y = 0; y < imageSize; ++y)
        for (UINT x = 0; x < imageSize; ++x)
            input[y * imageSize + x] = (static_cast<DWORD>(x * 3 + y) % 256 << 24) | softEdge(x) * 0x010101;
    double changeByLevel[4]{};
    for (uint32_t level = post_fx::sharpeningLow; level <= post_fx::sharpeningHigh; ++level) {
        settings.change(post_fx::sharpening, level);
        test::check("Soft edge uploads", image.upload(fixture.device, input));
        test::check("Sharpening draws on real D3D9", render(effects, fixture.device, image, settings));
        test::check("Sharpened output reads back", image.read(fixture.device, output));
        if (output.size() != input.size()) return 4;
        const unsigned row = 32 * imageSize;
        const int darkSide = static_cast<int>(output[row + 28] & 0xff), brightSide = static_cast<int>(output[row + 34] & 0xff);
        changeByLevel[level] = imageChange(input, output);
        printf("  Sharpening level %u: ramp ends %d/%d (input 64/192); mean change %.3f\n",
            level, darkSide, brightSide, changeByLevel[level]);
        test::check("Sharpening steepens the ramp", darkSide < 64 && brightSide > 192);
        test::check("Sharpening leaves flat regions and alpha unchanged", [&]() {
            for (UINT y = 0; y < imageSize; ++y)
                for (UINT x : {2u, 20u, 44u, 60u})
                    if (output[y * imageSize + x] != input[y * imageSize + x]) return false;
            for (UINT index = 0; index < input.size(); ++index)
                if ((input[index] >> 24) != (output[index] >> 24)) return false;
            return true;
        }());
    }
    test::check("Higher sharpening levels sharpen more",
        changeByLevel[1] < changeByLevel[2] && changeByLevel[2] < changeByLevel[3]);
    settings.change(post_fx::sharpening, post_fx::sharpeningOff);

    for (UINT y = 0; y < imageSize; ++y)
        for (UINT x = 0; x < imageSize; ++x)
            input[y * imageSize + x] = 0x7f000000 | (((x + y) % 2) ? 224u : 24u) * 0x010101;
    test::check("SSAA four-sample input uploads", image.upload(fixture.device, input));
    test::check("SSAA resolves independently of post-AA and blur", render(effects, fixture.device, image, settings, 2));
    test::check("SSAA output reads back", image.read(fixture.device, output));
    test::check("SSAA averages four samples in linear light and replicates resolved pixels without extra smoothing", [&]() {
        // sRGB 24 and 224 average to about 165 in linear light; a gamma-space average would give 124.
        constexpr DWORD linearAverageLow = 164, linearAverageHigh = 166;
        if (output.size() != input.size()) return false;
        const DWORD first = output[0];
        const DWORD channel = first & 0xff;
        if ((first >> 24) != 0x7f || channel < linearAverageLow || channel > linearAverageHigh) return false;
        if (first != (0x7f000000 | channel * 0x010101)) return false;
        for (DWORD pixel : output) if (pixel != first) return false;
        printf("  SSAA resolved value: %lu\n", static_cast<unsigned long>(channel));
        return true;
    }());
    settings.change(post_fx::antialiasing, post_fx::aaSmaa);
    test::check("SMAA plus SSAA input uploads", image.upload(fixture.device, input));
    test::check("SMAA and SSAA can run together", render(effects, fixture.device, image, settings, 2));
    effects.releaseResources();
    settings.change(post_fx::antialiasing, post_fx::aaOff);
    Image nativePolygon, highPolygon(imageSize * 2);
    std::vector<DWORD> nativePixels(imageSize * imageSize), highPixels(imageSize * imageSize * 4);
    test::check("Native polygon target is created", nativePolygon.upload(fixture.device, nativePixels));
    test::check("High-resolution polygon target is created", highPolygon.upload(fixture.device, highPixels));
    test::check("Native polygon is rasterized", drawPolygon(fixture.device, nativePolygon));
    test::check("Independent high-resolution polygon samples are rasterized", drawPolygon(fixture.device, highPolygon));
    test::check("Native polygon output reads back", nativePolygon.read(fixture.device, nativePixels));
    test::check("High-resolution polygon resolves through SSAA", render(effects, fixture.device, highPolygon, settings, 2));
    test::check("Resolved polygon output reads back", highPolygon.read(fixture.device, highPixels));
    test::check("SSAA produces partial polygon coverage absent from native rasterization", [&]() {
        unsigned partialNative = 0, partialResolved = 0;
        for (UINT y = 0; y < imageSize; ++y)
            for (UINT x = 0; x < imageSize; ++x) {
                const auto nativeValue = nativePixels[y * imageSize + x] & 0xff;
                const auto resolvedValue = highPixels[(y * 2) * (imageSize * 2) + x * 2] & 0xff;
                partialNative += nativeValue > 24 && nativeValue < 224;
                partialResolved += resolvedValue > 24 && resolvedValue < 224;
            }
        printf("  Polygon partial coverage: native=%u; SSAA=%u\n", partialNative, partialResolved);
        return partialNative == 0 && partialResolved > 20;
    }());
    effects.releaseResources();
    settings.change(post_fx::motionBlur, 1);
    for (UINT y = 0; y < imageSize; ++y)
        for (UINT x = 0; x < imageSize; ++x)
            input[y * imageSize + x] = 0x7f000000 | ((x / 3) % 2 ? 224u : 24u) * 0x010101;
    IDirect3DTexture9* depthTexture = nullptr;
    double changes[2]{};
    for (unsigned plane = 0; plane < 2; ++plane) {
        effects.releaseResources();
        const float distance = plane == 0 ? 8.0f : 80.0f;
        test::check("Synthetic planar depth uploads", uploadDepth(fixture.device, &depthTexture, [=](UINT, UINT) { return distance; }));
        test::check("Striped blur input uploads", image.upload(fixture.device, input));
        test::check("First camera sample seeds history", renderBlur(effects, fixture.device, image, depthTexture, settings, 0.15f, 2));
        test::check("Rotating camera draws motion blur", renderBlur(effects, fixture.device, image, depthTexture, settings, 0, 2.033));
        test::check("Blur output reads back", image.read(fixture.device, output));
        if (output.size() != input.size()) return 4;
        changes[plane] = imageChange(input, output);
        printf("  Plane distance: %.1f; mean interior color change: %.3f\n", distance, changes[plane]);
    }
    test::check("Distant planar scenery retains visible blur", changes[1] > 10);
    test::check("Near scenery blurs substantially less for identical projected camera motion", changes[0] < changes[1] * 0.5);
    post_fx::release(depthTexture);
    effects.releaseResources();
    FreeLibrary(module);
    puts("Standalone synthetic GPU test only; the hidden test window was never presented.");
    return test::finish();
}

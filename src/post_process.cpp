#include "post_process.h"
#include <cstring>
#include "../third_party/smaa/AreaTex.h"
#include "../third_party/smaa/SearchTex.h"

namespace post_fx {
namespace {
constexpr unsigned copyShader = 0, fxaaShader = 1, blurShader = 2;
constexpr unsigned edgesShader = 3, weightsShader = 4, neighborhoodShader = 5;
constexpr DWORD fullScreenVertexFormat = D3DFVF_XYZRHW | D3DFVF_TEX1;
struct ScreenVertex { float x, y, z, rhw, u, v; };
bool supportedDepth(D3DFORMAT format) {
    return format == D3DFMT_R32F || format == D3DFMT_G32R32F || format == D3DFMT_A32B32G32R32F ||
        format == D3DFMT_R16F || format == D3DFMT_G16R16F || format == D3DFMT_A16B16G16R16F ||
        format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8;
}
HRESULT loadLookup(IDirect3DDevice9* device, UINT width, UINT height, D3DFORMAT format,
    const unsigned char* bytes, UINT pitch, IDirect3DTexture9** output) {
    // Managed lookups are uploaded only when SMAA resources are prepared.
    HRESULT result = device->CreateTexture(width, height, 1, 0, format, D3DPOOL_MANAGED, output, nullptr);
    if (FAILED(result)) return result;
    D3DLOCKED_RECT locked{};
    result = (*output)->LockRect(0, &locked, nullptr, 0);
    if (FAILED(result)) return result;
    for (UINT row = 0; row < height; ++row)
        std::memcpy(static_cast<unsigned char*>(locked.pBits) + row * locked.Pitch, bytes + row * pitch, pitch);
    return (*output)->UnlockRect(0);
}
}
bool PostProcess::initialize(HMODULE module) {
    for (unsigned index = 0; index < std::size(code); ++index) {
        const auto resource = FindResourceW(module, MAKEINTRESOURCEW(101 + index), RT_RCDATA);
        const DWORD size = resource ? SizeofResource(module, resource) : 0;
        const auto loaded = resource ? LoadResource(module, resource) : nullptr;
        const auto* bytes = loaded ? static_cast<const DWORD*>(LockResource(loaded)) : nullptr;
        if (!bytes || size < 8 || size % sizeof(DWORD) || bytes[0] != 0xffff0300 ||
            bytes[size / sizeof(DWORD) - 1] != 0x0000ffff) return false;
        code[index].bytes = bytes;
    }
    return true;
}
void PostProcess::releaseResources() {
    for (auto& surface : scratchSurfaces) release(surface);
    for (auto& texture : scratch) release(texture);
    for (auto& shader : shaders) release(shader);
    for (auto& surface : smaaSurfaces) release(surface);
    for (auto& texture : smaaTextures) release(texture);
    release(areaTexture); release(searchTexture);
    release(resolvedSurface); release(resolvedTexture);
    device = nullptr;
    width = height = targetCount = 0;
    format = D3DFMT_UNKNOWN;
    suspended = false;
    history.clear();
}
HRESULT PostProcess::prepare(IDirect3DDevice9* currentDevice, const D3DSURFACE_DESC& description, unsigned factor) {
    if (factor != 1 && factor != 2) return E_INVALIDARG;
    if (device == currentDevice && width == description.Width && height == description.Height && format == description.Format &&
        samplingFactor == factor)
        return suspended ? E_FAIL : S_OK;
    releaseResources();
    device = currentDevice;
    width = description.Width;
    height = description.Height;
    format = description.Format;
    samplingFactor = factor;
    // Failures remain suspended until a device/target change or Reset.
    suspended = true;
    D3DCAPS9 caps{};
    HRESULT result = currentDevice->GetDeviceCaps(&caps);
    if (FAILED(result)) return result;
    if (caps.PixelShaderVersion < D3DPS_VERSION(3, 0) || !caps.NumSimultaneousRTs || caps.NumSimultaneousRTs > 4 ||
        description.MultiSampleType != D3DMULTISAMPLE_NONE || !description.Width || !description.Height) return D3DERR_NOTAVAILABLE;
    if (width % factor || height % factor) return D3DERR_NOTAVAILABLE;
    targetCount = caps.NumSimultaneousRTs;
    for (unsigned index = 0; index < std::size(shaders); ++index) {
        result = device->CreatePixelShader(code[index].bytes, &shaders[index]);
        if (FAILED(result)) return result;
    }
    for (unsigned index = 0; index < std::size(scratch); ++index) {
        result = device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &scratch[index], nullptr);
        if (FAILED(result)) return result;
        result = scratch[index]->GetSurfaceLevel(0, &scratchSurfaces[index]);
        if (FAILED(result)) return result;
    }
    if (factor == 2) {
        result = device->CreateTexture(width / factor, height / factor, 1, D3DUSAGE_RENDERTARGET, format,
            D3DPOOL_DEFAULT, &resolvedTexture, nullptr);
        if (FAILED(result)) return result;
        result = resolvedTexture->GetSurfaceLevel(0, &resolvedSurface);
        if (FAILED(result)) return result;
    }
    suspended = false;
    return S_OK;
}
HRESULT PostProcess::prepareSmaa() {
    if (searchTexture && smaaSurfaces[1]) return S_OK;
    for (unsigned index = 0; index < std::size(smaaTextures); ++index) {
        HRESULT result = device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT, &smaaTextures[index], nullptr);
        if (FAILED(result)) return result;
        result = smaaTextures[index]->GetSurfaceLevel(0, &smaaSurfaces[index]);
        if (FAILED(result)) return result;
    }
    HRESULT result = loadLookup(device, AREATEX_WIDTH, AREATEX_HEIGHT, D3DFMT_A8L8,
        areaTexBytes, AREATEX_PITCH, &areaTexture);
    if (FAILED(result)) return result;
    return loadLookup(device, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, D3DFMT_L8,
        searchTexBytes, SEARCHTEX_PITCH, &searchTexture);
}
HRESULT PostProcess::draw(IDirect3DSurface9* output, IDirect3DTexture9* input, IDirect3DTexture9* depth,
    IDirect3DPixelShader9* shader, UINT outputWidth, UINT outputHeight) {
    HRESULT result = device->SetTexture(0, nullptr);
    if (FAILED(result)) return result;
    result = device->SetTexture(1, nullptr);
    if (FAILED(result)) return result;
    result = device->SetRenderTarget(0, output);
    if (FAILED(result)) return result;
    if (!outputWidth) outputWidth = width;
    if (!outputHeight) outputHeight = height;
    const D3DVIEWPORT9 viewport{0, 0, outputWidth, outputHeight, 0, 1};
    result = device->SetViewport(&viewport);
    if (FAILED(result)) return result;
    result = device->SetTexture(0, input);
    if (FAILED(result)) return result;
    result = device->SetTexture(1, depth);
    if (FAILED(result)) return result;
    result = device->SetPixelShader(shader);
    if (FAILED(result)) return result;
    // XYZRHW pixel centers in D3D9: -0.5 puts the first texel center at pixel zero.
    const float right = static_cast<float>(outputWidth) - 0.5f, bottom = static_cast<float>(outputHeight) - 0.5f;
    const ScreenVertex vertices[] = {{-0.5f, -0.5f, 0, 1, 0, 0}, {right, -0.5f, 0, 1, 1, 0},
        {-0.5f, bottom, 0, 1, 0, 1}, {right, bottom, 0, 1, 1, 1}};
    return device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(ScreenVertex));
}
HRESULT PostProcess::applySmaa(IDirect3DTexture9* input, unsigned output) {
    // Edge detection discards flat pixels. Clear every draw to remove old edges.
    HRESULT status = device->SetRenderTarget(0, smaaSurfaces[0]);
    if (FAILED(status)) return status;
    const D3DVIEWPORT9 viewport{0, 0, width, height, 0, 1};
    status = device->SetViewport(&viewport);
    if (FAILED(status)) return status;
    status = device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0);
    if (FAILED(status)) return status;
    status = draw(smaaSurfaces[0], input, nullptr, shaders[edgesShader]);
    if (FAILED(status)) return status;
    for (auto filter : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER}) {
        status = device->SetSamplerState(1, filter, D3DTEXF_LINEAR);
        if (FAILED(status)) return status;
        status = device->SetSamplerState(2, filter, D3DTEXF_POINT);
        if (FAILED(status)) return status;
    }
    status = device->SetTexture(2, searchTexture);
    if (FAILED(status)) return status;
    status = draw(smaaSurfaces[1], smaaTextures[0], areaTexture, shaders[weightsShader]);
    if (FAILED(status)) return status;
    status = device->SetTexture(2, nullptr);
    if (FAILED(status)) return status;
    // Standard display-color targets need linear-space neighborhood blending;
    // float/HDR targets already contain linear values. Lookups stay non-sRGB.
    const DWORD displayColor = format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8;
    status = device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, displayColor);
    if (FAILED(status)) return status;
    status = device->SetRenderState(D3DRS_SRGBWRITEENABLE, displayColor);
    if (FAILED(status)) return status;
    status = draw(scratchSurfaces[output], input, smaaTextures[1], shaders[neighborhoodShader]);
    if (FAILED(status)) return status;
    status = device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    if (FAILED(status)) return status;
    return device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
}
HRESULT PostProcess::resolveSamples(IDirect3DTexture9* input, IDirect3DSurface9* colorSurface) {
    // A 2:1 bilinear reduction averages the four independently rendered samples.
    HRESULT status = draw(resolvedSurface, input, nullptr, shaders[copyShader], width / samplingFactor, height / samplingFactor);
    if (FAILED(status)) return status;
    for (auto filter : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER}) {
        status = device->SetSamplerState(0, filter, D3DTEXF_POINT);
        if (FAILED(status)) return status;
    }
    // Replicate each resolved pixel into a 2x2 block. The game's existing
    // scene-copy preserves that value with either point or linear sampling.
    return draw(colorSurface, resolvedTexture, nullptr, shaders[copyShader]);
}
HRESULT PostProcess::render(IDirect3DDevice9* currentDevice, IDirect3DTexture9* color, IDirect3DTexture9* depth,
    const CameraSample& camera, double seconds, const Settings& settings, unsigned factor) {
    if (!settings.enabled() && factor == 1) { history.clear(); return S_FALSE; }
    if (!currentDevice || !color) { history.clear(); return S_FALSE; }
    HRESULT result = currentDevice->TestCooperativeLevel();
    if (FAILED(result)) { releaseResources(); return result; }
    D3DSURFACE_DESC description{};
    result = color->GetLevelDesc(0, &description);
    if (FAILED(result)) return result;
    result = prepare(currentDevice, description, factor);
    if (FAILED(result)) return result;
    if (suspended) return E_FAIL;
    if (settings.value(antialiasing) == aaSmaa) {
        result = prepareSmaa();
        if (FAILED(result)) { suspended = true; return result; }
    }
    CameraSample usableCamera = camera;
    D3DSURFACE_DESC depthDescription{};
    usableCamera.valid = usableCamera.valid && settings.value(motionBlur) && depth &&
        SUCCEEDED(depth->GetLevelDesc(0, &depthDescription)) && supportedDepth(depthDescription.Format) &&
        depthDescription.Width == width && depthDescription.Height == height;
    const Exposure exposure = history.advance(usableCamera, seconds, settings.revision(), settings.value(blurStrength));
    if (!exposure.valid && !settings.value(antialiasing) && factor == 1) return S_FALSE;
    IDirect3DSurface9* colorSurface = nullptr;
    result = color->GetSurfaceLevel(0, &colorSurface);
    if (FAILED(result)) return result;
    DeviceState saved(device);
    if (!saved.capture(targetCount)) { release(colorSurface); suspended = true; return E_FAIL; }
    auto renderPasses = [&]() -> HRESULT {
        HRESULT status = device->SetTexture(0, nullptr);
        if (FAILED(status)) return status;
        status = device->SetTexture(1, nullptr);
        if (FAILED(status)) return status;
        status = device->SetTexture(2, nullptr);
        if (FAILED(status)) return status;
        status = device->SetDepthStencilSurface(nullptr);
        if (FAILED(status)) return status;
        for (unsigned index = 1; index < targetCount; ++index) {
            status = device->SetRenderTarget(index, nullptr);
            if (FAILED(status)) return status;
        }
        status = device->SetVertexShader(nullptr);
        if (FAILED(status)) return status;
        status = device->SetFVF(fullScreenVertexFormat);
        if (FAILED(status)) return status;
        status = device->SetStreamSourceFreq(0, 1);
        if (FAILED(status)) return status;
        for (const auto& setting : passStates) {
            status = device->SetRenderState(setting.state, setting.value);
            if (FAILED(status)) return status;
        }
        const DWORD sampling[] = {D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, D3DTEXF_LINEAR, D3DTEXF_LINEAR,
            D3DTEXF_NONE, FALSE, 0, 0, 1};
        for (unsigned sampler = 0; sampler < effectSamplerCount; ++sampler)
            for (unsigned index = 0; index < std::size(samplerStates); ++index) {
                DWORD value = sampling[index];
                if (sampler == 1 && (samplerStates[index] == D3DSAMP_MINFILTER || samplerStates[index] == D3DSAMP_MAGFILTER))
                    value = D3DTEXF_POINT;
                status = device->SetSamplerState(sampler, samplerStates[index], value);
                if (FAILED(status)) return status;
            }
        const float dimensions[] = {1.0f / width, 1.0f / height, static_cast<float>(width), static_cast<float>(height)};
        status = device->SetPixelShaderConstantF(8, dimensions, 1);
        if (FAILED(status)) return status;
        IDirect3DTexture9* input = color;
        unsigned output = 0;
        if (exposure.valid) {
            status = device->SetPixelShaderConstantF(0, &exposure.inverseCurrent.elements[0][0], 4);
            if (FAILED(status)) return status;
            status = device->SetPixelShaderConstantF(4, &exposure.previous.elements[0][0], 4);
            if (FAILED(status)) return status;
            constexpr float samples[] = {4, 8, 12};
            const float parameters[] = {exposure.scale, 32.0f * factor, samples[settings.value(blurQuality)], 0.02f};
            status = device->SetPixelShaderConstantF(9, parameters, 1);
            if (FAILED(status)) return status;
            const float origin[] = {camera.position[0], camera.position[1], camera.position[2], 1};
            status = device->SetPixelShaderConstantF(10, origin, 1);
            if (FAILED(status)) return status;
            // Camera motion produces the strongest apparent velocity in the foreground.
            // Fade its exposure and width independently so distant scenery keeps its blur.
            const float nearBlur[] = {8.0f, 50.0f, 0.1f, 3.0f * factor};
            status = device->SetPixelShaderConstantF(11, nearBlur, 1);
            if (FAILED(status)) return status;
            const float forward[] = {camera.view.elements[0][2], camera.view.elements[1][2], camera.view.elements[2][2], 0};
            status = device->SetPixelShaderConstantF(12, forward, 1);
            if (FAILED(status)) return status;
            status = draw(scratchSurfaces[output], input, depth, shaders[blurShader]);
            if (FAILED(status)) return status;
            input = scratch[output++];
        }
        if (settings.value(antialiasing) == aaFxaa) {
            status = draw(scratchSurfaces[output], input, nullptr, shaders[fxaaShader]);
            if (FAILED(status)) return status;
            input = scratch[output];
        }
        if (settings.value(antialiasing) == aaSmaa) {
            status = applySmaa(input, output);
            if (FAILED(status)) return status;
            input = scratch[output];
        }
        // The game's texture is written only after every effect pass succeeds.
        return factor == 2 ? resolveSamples(input, colorSurface) : draw(colorSurface, input, nullptr, shaders[copyShader]);
    };
    result = renderPasses();
    const bool restored = saved.restore();
    release(colorSurface);
    if (FAILED(result) || !restored) { suspended = true; history.clear(); }
    return restored ? result : E_FAIL;
}
}

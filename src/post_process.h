#pragma once
#include "device_state.h"
#include "motion_blur.h"
#include "settings.h"
#include "supersampling.h"

namespace post_fx {
// render() returns this when the device state that the passes change could not be read back.
inline constexpr HRESULT stateCaptureFailed = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x201);
// The renderer did not open the display group, so the 1.5x SSAA image could not be drawn.
inline constexpr HRESULT displayGroupUnavailable = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x202);

class PostProcess {
public:
    bool initialize(HMODULE module);
    void releaseResources();
    void clearHistory() { history.clear(); }
    // S_FALSE: nothing was drawn. A failure suspends the failed passes until device Reset
    // or a target size/format change; a failed SMAA setup stops only SMAA.
    HRESULT render(IDirect3DDevice9* device, IDirect3DTexture9* color, IDirect3DTexture9* depth,
        const CameraSample& camera, double seconds, const Settings& settings, const SceneSize& scene = {});
    // With 1.5x SSAA, render() leaves the game's scene texture as it is and keeps the resolved
    // native image for presentResolved(), which draws it into the current render target (the
    // display group) after the game's own copy. S_FALSE: nothing is waiting.
    bool resolvedWaiting() const { return resolvedPending; }
    void discardResolved() { resolvedPending = false; }
    HRESULT presentResolved(IDirect3DDevice9* device);
    bool failed() const { return suspended; }
private:
    // Same order as the RCDATA numbers in shaders.rc.in.
    enum ShaderIndex : unsigned {
        copyShader, fxaaShader, blurShader, edgesShader, weightsShader, neighborhoodShader, sharpenShader, areaResolveShader,
        shaderCount
    };
    HRESULT prepare(IDirect3DDevice9* device, const D3DSURFACE_DESC& description, const SceneSize& scene);
    HRESULT prepareSmaa();
    HRESULT ensureSmaa();
    void releaseSmaa();
    bool depthUsable(IDirect3DTexture9* depth) const;
    Exposure advanceHistory(const CameraSample& camera, IDirect3DTexture9* depth, double seconds, const Settings& settings);
    HRESULT renderPasses(IDirect3DTexture9* color, IDirect3DTexture9* depth, IDirect3DSurface9* colorSurface,
        const Exposure& exposure, const CameraSample& camera, uint32_t method, const Settings& settings);
    HRESULT sampleDepthByPoint();
    HRESULT bindPassState();
    HRESULT applyMotionBlur(IDirect3DTexture9* input, IDirect3DTexture9* depth, const Exposure& exposure,
        const CameraSample& camera, uint32_t quality, unsigned output);
    HRESULT applySmaa(IDirect3DTexture9* input, IDirect3DTexture9* depth, unsigned output);
    HRESULT applySharpening(IDirect3DTexture9* input, uint32_t level, unsigned output);
    HRESULT resolveSamples(IDirect3DTexture9* input, IDirect3DSurface9* colorSurface);
    HRESULT resolveByArea(IDirect3DTexture9* input);
    HRESULT sampleColorByPoint();
    HRESULT setLinearBlending(bool enabled);
    HRESULT draw(IDirect3DSurface9* output, IDirect3DTexture9* input, IDirect3DTexture9* depth,
        IDirect3DPixelShader9* shader, UINT outputWidth = 0, UINT outputHeight = 0);
    const DWORD* shaderCode[shaderCount]{};
    IDirect3DPixelShader9* shaders[shaderCount]{};
    IDirect3DTexture9* scratch[2]{};
    IDirect3DSurface9* scratchSurfaces[2]{};
    IDirect3DTexture9* smaaTextures[2]{};
    IDirect3DSurface9* smaaSurfaces[2]{};
    IDirect3DTexture9* areaTexture = nullptr;
    IDirect3DTexture9* searchTexture = nullptr;
    IDirect3DTexture9* resolvedTexture = nullptr;
    IDirect3DSurface9* resolvedSurface = nullptr;
    IDirect3DDevice9* device = nullptr;
    UINT width = 0, height = 0, targetCount = 0;
    SupersamplingMode samplingMode = supersamplingOff;
    UINT nativeWidth = 0, nativeHeight = 0;
    bool resolvedPending = false;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    // Blend gamma-encoded 8-bit colors in linear space when the device can decode/encode sRGB.
    bool linearBlending = false;
    bool suspended = false;
    bool smaaUnavailable = false;
    MotionHistory history;
};
}

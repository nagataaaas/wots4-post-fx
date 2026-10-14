#pragma once
#include "device_state.h"
#include "motion_blur.h"
#include "settings.h"

namespace post_fx {
// render() returns this when the device state that the passes change could not be read back.
inline constexpr HRESULT stateCaptureFailed = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x201);

class PostProcess {
public:
    bool initialize(HMODULE module);
    void releaseResources();
    void clearHistory() { history.clear(); }
    // S_FALSE: nothing was drawn. A failure suspends the failed passes until device Reset
    // or a target size/format change; a failed SMAA setup stops only SMAA.
    HRESULT render(IDirect3DDevice9* device, IDirect3DTexture9* color, IDirect3DTexture9* depth,
        const CameraSample& camera, double seconds, const Settings& settings, unsigned supersampleFactor = 1);
    bool failed() const { return suspended; }
private:
    // Same order as the RCDATA numbers in shaders.rc.in.
    enum ShaderIndex : unsigned { copyShader, fxaaShader, blurShader, edgesShader, weightsShader, neighborhoodShader, shaderCount };
    HRESULT prepare(IDirect3DDevice9* device, const D3DSURFACE_DESC& description, unsigned supersampleFactor);
    HRESULT prepareSmaa();
    HRESULT ensureSmaa();
    void releaseSmaa();
    bool depthUsable(IDirect3DTexture9* depth) const;
    Exposure advanceHistory(const CameraSample& camera, IDirect3DTexture9* depth, double seconds, const Settings& settings);
    HRESULT renderPasses(IDirect3DTexture9* color, IDirect3DTexture9* depth, IDirect3DSurface9* colorSurface,
        const Exposure& exposure, const CameraSample& camera, uint32_t method, uint32_t quality);
    HRESULT bindPassState();
    HRESULT applyMotionBlur(IDirect3DTexture9* input, IDirect3DTexture9* depth, const Exposure& exposure,
        const CameraSample& camera, uint32_t quality, unsigned output);
    HRESULT applySmaa(IDirect3DTexture9* input, unsigned output);
    HRESULT resolveSamples(IDirect3DTexture9* input, IDirect3DSurface9* colorSurface);
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
    unsigned samplingFactor = 1;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    bool suspended = false;
    bool smaaUnavailable = false;
    MotionHistory history;
};
}

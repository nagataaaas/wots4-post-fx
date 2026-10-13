#pragma once
#include "device_state.h"
#include "motion_blur.h"
#include "settings.h"

namespace post_fx {
class PostProcess {
public:
    bool initialize(HMODULE module);
    void releaseResources();
    void clearHistory() { history.clear(); }
    HRESULT render(IDirect3DDevice9* device, IDirect3DTexture9* color, IDirect3DTexture9* depth,
        const CameraSample& camera, double seconds, const Settings& settings, unsigned supersampleFactor = 1);
    bool failed() const { return suspended; }
private:
    HRESULT prepare(IDirect3DDevice9* device, const D3DSURFACE_DESC& description, unsigned supersampleFactor);
    HRESULT prepareSmaa();
    HRESULT applySmaa(IDirect3DTexture9* input, unsigned output);
    HRESULT resolveSamples(IDirect3DTexture9* input, IDirect3DSurface9* colorSurface);
    HRESULT draw(IDirect3DSurface9* output, IDirect3DTexture9* input, IDirect3DTexture9* depth,
        IDirect3DPixelShader9* shader, UINT outputWidth = 0, UINT outputHeight = 0);
    struct ShaderCode { const DWORD* bytes = nullptr; };
    ShaderCode code[6];
    IDirect3DPixelShader9* shaders[6]{};
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
    MotionHistory history;
};
}

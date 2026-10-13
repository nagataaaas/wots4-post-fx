#pragma once
#include <d3d9.h>
#include <array>

namespace post_fx {
template<class Interface> void release(Interface*& object) {
    if (object) object->Release();
    object = nullptr;
}
struct RenderSetting { D3DRENDERSTATETYPE state; DWORD value; };
inline constexpr RenderSetting passStates[] = {
    {D3DRS_ZENABLE, FALSE}, {D3DRS_ZWRITEENABLE, FALSE}, {D3DRS_ALPHABLENDENABLE, FALSE},
    {D3DRS_ALPHATESTENABLE, FALSE}, {D3DRS_CULLMODE, D3DCULL_NONE}, {D3DRS_STENCILENABLE, FALSE},
    {D3DRS_SCISSORTESTENABLE, FALSE}, {D3DRS_FOGENABLE, FALSE}, {D3DRS_LIGHTING, FALSE},
    {D3DRS_SRGBWRITEENABLE, FALSE}, {D3DRS_COLORWRITEENABLE, 0xf}, {D3DRS_FILLMODE, D3DFILL_SOLID},
    {D3DRS_MULTISAMPLEANTIALIAS, FALSE}, {D3DRS_SEPARATEALPHABLENDENABLE, FALSE},
    {D3DRS_CLIPPLANEENABLE, 0}, {D3DRS_SHADEMODE, D3DSHADE_GOURAUD},
};
inline constexpr D3DSAMPLERSTATETYPE samplerStates[] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
    D3DSAMP_SRGBTEXTURE, D3DSAMP_MAXMIPLEVEL, D3DSAMP_MIPMAPLODBIAS, D3DSAMP_MAXANISOTROPY,
};
inline constexpr unsigned shaderConstantCount = 13;
inline constexpr unsigned effectSamplerCount = 3;

// Saves only states changed by our passes. References are released within this draw,
// so a persistent state block cannot keep the game's old textures alive across Reset.
class DeviceState {
public:
    explicit DeviceState(IDirect3DDevice9* device) : device(device) {}
    ~DeviceState();
    DeviceState(const DeviceState&) = delete;
    DeviceState& operator=(const DeviceState&) = delete;
    bool capture(unsigned targetCount);
    bool restore();
private:
    void releaseReferences();
    IDirect3DDevice9* device;
    unsigned targets = 0;
    bool captured = false;
    IDirect3DSurface9* renderTargets[4]{};
    IDirect3DSurface9* depthStencil = nullptr;
    IDirect3DBaseTexture9* textures[effectSamplerCount]{};
    IDirect3DPixelShader9* pixelShader = nullptr;
    IDirect3DVertexShader9* vertexShader = nullptr;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    IDirect3DVertexBuffer9* vertexBuffer = nullptr;
    UINT offset = 0, stride = 0;
    UINT frequency = 1;
    DWORD fvf = 0;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    DWORD renderValues[std::size(passStates)]{};
    DWORD samplerValues[effectSamplerCount][std::size(samplerStates)]{};
    float constants[shaderConstantCount * 4]{};
};
}

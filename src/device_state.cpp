#include "device_state.h"

namespace post_fx {
DeviceState::~DeviceState() {
    if (captured) restore();
    releaseReferences();
}
void DeviceState::releaseReferences() {
    for (auto& target : renderTargets) release(target);
    release(depthStencil);
    for (auto& texture : textures) release(texture);
    release(pixelShader);
    release(vertexShader);
    release(declaration);
    release(vertexBuffer);
}
bool DeviceState::capture(unsigned targetCount) {
    if (targetCount == 0 || targetCount > std::size(renderTargets) || captured) return false;
    targets = targetCount;
    for (unsigned index = 0; index < targets; ++index) {
        const HRESULT result = device->GetRenderTarget(index, &renderTargets[index]);
        if (FAILED(result) && (index == 0 || result != D3DERR_NOTFOUND)) return false;
    }
    const HRESULT depthResult = device->GetDepthStencilSurface(&depthStencil);
    if (FAILED(depthResult) && depthResult != D3DERR_NOTFOUND) return false;
    if (FAILED(device->GetViewport(&viewport)) || FAILED(device->GetScissorRect(&scissor)) ||
        FAILED(device->GetPixelShader(&pixelShader)) || FAILED(device->GetVertexShader(&vertexShader)) ||
        FAILED(device->GetVertexDeclaration(&declaration)) || FAILED(device->GetFVF(&fvf)) ||
        FAILED(device->GetStreamSource(0, &vertexBuffer, &offset, &stride)) ||
        FAILED(device->GetStreamSourceFreq(0, &frequency)) ||
        FAILED(device->GetPixelShaderConstantF(0, constants, shaderConstantCount))) return false;
    for (unsigned index = 0; index < std::size(passStates); ++index)
        if (FAILED(device->GetRenderState(passStates[index].state, &renderValues[index]))) return false;
    for (unsigned sampler = 0; sampler < effectSamplerCount; ++sampler) {
        if (FAILED(device->GetTexture(sampler, &textures[sampler]))) return false;
        for (unsigned index = 0; index < std::size(passSamplerStates); ++index) {
            const HRESULT result = device->GetSamplerState(sampler, passSamplerStates[index].state,
                &samplerValues[sampler][index]);
            if (FAILED(result)) return false;
        }
    }
    captured = true;
    return true;
}
bool DeviceState::restore() {
    if (!captured) return false;
    bool succeeded = true;
    auto record = [&succeeded](HRESULT result) { if (FAILED(result)) succeeded = false; };
    // Unbind scratch samplers before restoring any target they might alias.
    for (unsigned sampler = 0; sampler < effectSamplerCount; ++sampler) record(device->SetTexture(sampler, nullptr));
    record(device->SetDepthStencilSurface(nullptr));
    for (unsigned index = 1; index < targets; ++index) record(device->SetRenderTarget(index, nullptr));
    record(device->SetRenderTarget(0, renderTargets[0]));
    for (unsigned index = 1; index < targets; ++index) record(device->SetRenderTarget(index, renderTargets[index]));
    record(device->SetDepthStencilSurface(depthStencil));
    record(device->SetViewport(&viewport));
    record(device->SetScissorRect(&scissor));
    record(device->SetVertexShader(vertexShader));
    record(fvf ? device->SetFVF(fvf) : device->SetVertexDeclaration(declaration));
    record(device->SetStreamSource(0, vertexBuffer, offset, stride));
    record(device->SetStreamSourceFreq(0, frequency));
    record(device->SetPixelShader(pixelShader));
    record(device->SetPixelShaderConstantF(0, constants, shaderConstantCount));
    for (unsigned index = 0; index < std::size(passStates); ++index)
        record(device->SetRenderState(passStates[index].state, renderValues[index]));
    // SetRenderState also restores the blend/Z-write values tracked by other Mods.
    // COLORWRITEENABLE1 is deliberately never set: its requested mask can differ
    // from the effective mask currently reported by the driver.
    for (unsigned sampler = 0; sampler < effectSamplerCount; ++sampler) {
        for (unsigned index = 0; index < std::size(passSamplerStates); ++index)
            record(device->SetSamplerState(sampler, passSamplerStates[index].state, samplerValues[sampler][index]));
        record(device->SetTexture(sampler, textures[sampler]));
    }
    captured = false;
    releaseReferences();
    return succeeded;
}
}

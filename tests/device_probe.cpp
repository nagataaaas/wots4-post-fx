#include "post_process.h"
#include "test_support.h"
#include <cstring>
#include <vector>

namespace {
struct Object {
    void** table = nullptr;
    ULONG references = 1;
    D3DSURFACE_DESC description{};
    Object* surface = nullptr;
    std::vector<unsigned char> pixels;
};
void* objectTable[22]{};
ULONG STDMETHODCALLTYPE addReference(Object* object) { return ++object->references; }
ULONG STDMETHODCALLTYPE releaseReference(Object* object) {
    const ULONG remaining = --object->references;
    if (!remaining && object->surface) releaseReference(object->surface);
    return remaining;
}
HRESULT STDMETHODCALLTYPE levelDescription(Object* object, UINT, D3DSURFACE_DESC* description) {
    *description = object->description; return S_OK;
}
HRESULT STDMETHODCALLTYPE getSurface(Object* object, UINT, IDirect3DSurface9** surface) {
    *surface = reinterpret_cast<IDirect3DSurface9*>(object->surface);
    addReference(object->surface); return S_OK;
}
HRESULT STDMETHODCALLTYPE lockTexture(Object* object, UINT, D3DLOCKED_RECT* locked, const RECT*, DWORD) {
    const UINT texelBytes = object->description.Format == D3DFMT_A8L8 ? 2 : 1;
    locked->Pitch = object->description.Width * texelBytes;
    object->pixels.resize(locked->Pitch * object->description.Height);
    locked->pBits = object->pixels.data();
    return S_OK;
}
HRESULT STDMETHODCALLTYPE unlockTexture(Object*, UINT) { return S_OK; }
template<class Interface> HRESULT reference(Object* object, Interface** output) {
    *output = reinterpret_cast<Interface*>(object);
    if (object) addReference(object);
    return S_OK;
}
Object* objectFrom(void* object) { return static_cast<Object*>(object); }
struct FakeDevice {
    void** table;
    Object* targets[4]{};
    Object* depth = nullptr;
    Object* textures[post_fx::effectSamplerCount]{};
    Object* vertexShader = nullptr;
    Object* pixelShader = nullptr;
    Object* declaration = nullptr;
    Object* stream = nullptr;
    D3DVIEWPORT9 viewport{2, 3, 80, 60, 0.1f, 0.9f};
    RECT scissor{1, 2, 70, 50};
    DWORD renderStates[256]{};
    DWORD samplerStates[post_fx::effectSamplerCount][16]{};
    float constants[post_fx::shaderConstantCount * 4]{};
    DWORD fvf = 0, frequency = 2;
    UINT offset = 12, stride = 24;
    unsigned draws = 0, creates = 0, clears = 0, forbiddenMasks = 0, unexpected = 0;
    unsigned failDraw = 0;
    bool failCapture = false, lost = false, failCreate = false, alias = false;
    bool blending = true, zWriting = true;
    DWORD requestedMask = 0xf;
    std::vector<Object*> created;
};
HRESULT STDMETHODCALLTYPE unexpected(FakeDevice* device) { ++device->unexpected; return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE cooperative(FakeDevice* device) { return device->lost ? D3DERR_DEVICELOST : S_OK; }
HRESULT STDMETHODCALLTYPE caps(FakeDevice*, D3DCAPS9* output) {
    memset(output, 0, sizeof(*output)); output->NumSimultaneousRTs = 4; output->PixelShaderVersion = D3DPS_VERSION(3, 0); return S_OK;
}
HRESULT STDMETHODCALLTYPE getTarget(FakeDevice* device, DWORD index, IDirect3DSurface9** output) {
    reference(device->targets[index], output); return device->targets[index] ? S_OK : D3DERR_NOTFOUND;
}
HRESULT STDMETHODCALLTYPE setTarget(FakeDevice* device, DWORD index, IDirect3DSurface9* output) {
    device->targets[index] = objectFrom(output); return S_OK;
}
HRESULT STDMETHODCALLTYPE getDepth(FakeDevice* device, IDirect3DSurface9** output) { return reference(device->depth, output); }
HRESULT STDMETHODCALLTYPE setDepth(FakeDevice* device, IDirect3DSurface9* output) { device->depth = objectFrom(output); return S_OK; }
HRESULT STDMETHODCALLTYPE getViewport(FakeDevice* device, D3DVIEWPORT9* output) { *output = device->viewport; return S_OK; }
HRESULT STDMETHODCALLTYPE setViewport(FakeDevice* device, const D3DVIEWPORT9* output) { device->viewport = *output; return S_OK; }
HRESULT STDMETHODCALLTYPE getScissor(FakeDevice* device, RECT* output) { *output = device->scissor; return S_OK; }
HRESULT STDMETHODCALLTYPE setScissor(FakeDevice* device, const RECT* output) { device->scissor = *output; return S_OK; }
HRESULT STDMETHODCALLTYPE getRenderState(FakeDevice* device, D3DRENDERSTATETYPE state, DWORD* output) {
    if (device->failCapture) return E_FAIL;
    *output = device->renderStates[state]; return S_OK;
}
HRESULT STDMETHODCALLTYPE setRenderState(FakeDevice* device, D3DRENDERSTATETYPE state, DWORD value) {
    if (state == D3DRS_COLORWRITEENABLE1) { ++device->forbiddenMasks; device->requestedMask = value; }
    if (state == D3DRS_ALPHABLENDENABLE) device->blending = value != 0;
    if (state == D3DRS_ZWRITEENABLE) device->zWriting = value != 0;
    device->renderStates[state] = value;
    device->renderStates[D3DRS_COLORWRITEENABLE1] = device->blending && !device->zWriting ? 0 : device->requestedMask;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE getTexture(FakeDevice* device, DWORD sampler, IDirect3DBaseTexture9** output) {
    return reference(device->textures[sampler], output);
}
HRESULT STDMETHODCALLTYPE setTexture(FakeDevice* device, DWORD sampler, IDirect3DBaseTexture9* output) {
    device->textures[sampler] = objectFrom(output); return S_OK;
}
HRESULT STDMETHODCALLTYPE getSampler(FakeDevice* device, DWORD sampler, D3DSAMPLERSTATETYPE state, DWORD* output) {
    *output = device->samplerStates[sampler][state]; return S_OK;
}
HRESULT STDMETHODCALLTYPE setSampler(FakeDevice* device, DWORD sampler, D3DSAMPLERSTATETYPE state, DWORD output) {
    device->samplerStates[sampler][state] = output; return S_OK;
}
HRESULT STDMETHODCALLTYPE getDeclaration(FakeDevice* device, IDirect3DVertexDeclaration9** output) {
    return reference(device->declaration, output);
}
HRESULT STDMETHODCALLTYPE setDeclaration(FakeDevice* device, IDirect3DVertexDeclaration9* output) {
    device->declaration = objectFrom(output); device->fvf = 0; return S_OK;
}
HRESULT STDMETHODCALLTYPE getFVF(FakeDevice* device, DWORD* output) { *output = device->fvf; return S_OK; }
HRESULT STDMETHODCALLTYPE setFVF(FakeDevice* device, DWORD output) { device->fvf = output; return S_OK; }
HRESULT STDMETHODCALLTYPE getVertexShader(FakeDevice* device, IDirect3DVertexShader9** output) { return reference(device->vertexShader, output); }
HRESULT STDMETHODCALLTYPE setVertexShader(FakeDevice* device, IDirect3DVertexShader9* output) { device->vertexShader = objectFrom(output); return S_OK; }
HRESULT STDMETHODCALLTYPE getPixelShader(FakeDevice* device, IDirect3DPixelShader9** output) { return reference(device->pixelShader, output); }
HRESULT STDMETHODCALLTYPE setPixelShader(FakeDevice* device, IDirect3DPixelShader9* output) { device->pixelShader = objectFrom(output); return S_OK; }
HRESULT STDMETHODCALLTYPE getConstants(FakeDevice* device, UINT index, float* output, UINT count) {
    memcpy(output, device->constants + index * 4, count * 4 * sizeof(float)); return S_OK;
}
HRESULT STDMETHODCALLTYPE setConstants(FakeDevice* device, UINT index, const float* output, UINT count) {
    memcpy(device->constants + index * 4, output, count * 4 * sizeof(float)); return S_OK;
}
HRESULT STDMETHODCALLTYPE getStream(FakeDevice* device, UINT, IDirect3DVertexBuffer9** output, UINT* offset, UINT* stride) {
    *offset = device->offset; *stride = device->stride; return reference(device->stream, output);
}
HRESULT STDMETHODCALLTYPE setStream(FakeDevice* device, UINT, IDirect3DVertexBuffer9* output, UINT offset, UINT stride) {
    device->stream = objectFrom(output); device->offset = offset; device->stride = stride; return S_OK;
}
HRESULT STDMETHODCALLTYPE getFrequency(FakeDevice* device, UINT, UINT* output) { *output = device->frequency; return S_OK; }
HRESULT STDMETHODCALLTYPE setFrequency(FakeDevice* device, UINT, UINT output) { device->frequency = output; return S_OK; }
Object* createObject(FakeDevice* device) {
    auto* object = new Object;
    object->table = objectTable;
    device->created.push_back(object);
    return object;
}
HRESULT STDMETHODCALLTYPE createShader(FakeDevice* device, const DWORD* code, IDirect3DPixelShader9** output) {
    ++device->creates;
    if (device->failCreate || !code || code[0] != 0xffff0300) return E_FAIL;
    *output = reinterpret_cast<IDirect3DPixelShader9*>(createObject(device)); return S_OK;
}
HRESULT STDMETHODCALLTYPE createTexture(FakeDevice* device, UINT width, UINT height, UINT, DWORD, D3DFORMAT format,
    D3DPOOL pool, IDirect3DTexture9** output, HANDLE*) {
    ++device->creates;
    auto* texture = createObject(device);
    texture->surface = createObject(device);
    texture->description.Width = width; texture->description.Height = height;
    texture->description.Format = format; texture->description.Pool = pool;
    *output = reinterpret_cast<IDirect3DTexture9*>(texture); return S_OK;
}
HRESULT STDMETHODCALLTYPE draw(FakeDevice* device, D3DPRIMITIVETYPE, UINT, const void* vertices, UINT stride) {
    ++device->draws;
    for (const auto* texture : device->textures)
        if (texture && texture->surface == device->targets[0]) device->alias = true;
    const float* vertex = static_cast<const float*>(vertices);
    test::check("Full-screen draw uses D3D9 half-texel coordinates", vertex[0] == -0.5f && vertex[1] == -0.5f && stride == 24);
    device->stream = nullptr; device->offset = device->stride = 0;
    return device->failDraw == device->draws ? E_FAIL : S_OK;
}
HRESULT STDMETHODCALLTYPE clearTarget(FakeDevice* device, DWORD, const D3DRECT*, DWORD flags, D3DCOLOR color, float, DWORD) {
    ++device->clears;
    return flags == D3DCLEAR_TARGET && color == 0 ? S_OK : E_FAIL;
}
void* deviceTable[119]{};
void initializeTables() {
    objectTable[1] = reinterpret_cast<void*>(addReference);
    objectTable[2] = reinterpret_cast<void*>(releaseReference);
    objectTable[17] = reinterpret_cast<void*>(levelDescription);
    objectTable[18] = reinterpret_cast<void*>(getSurface);
    objectTable[19] = reinterpret_cast<void*>(lockTexture);
    objectTable[20] = reinterpret_cast<void*>(unlockTexture);
    for (auto& entry : deviceTable) entry = reinterpret_cast<void*>(unexpected);
    deviceTable[3] = reinterpret_cast<void*>(cooperative); deviceTable[7] = reinterpret_cast<void*>(caps);
    deviceTable[23] = reinterpret_cast<void*>(createTexture);
    deviceTable[37] = reinterpret_cast<void*>(setTarget); deviceTable[38] = reinterpret_cast<void*>(getTarget);
    deviceTable[39] = reinterpret_cast<void*>(setDepth); deviceTable[40] = reinterpret_cast<void*>(getDepth);
    deviceTable[43] = reinterpret_cast<void*>(clearTarget);
    deviceTable[47] = reinterpret_cast<void*>(setViewport); deviceTable[48] = reinterpret_cast<void*>(getViewport);
    deviceTable[57] = reinterpret_cast<void*>(setRenderState); deviceTable[58] = reinterpret_cast<void*>(getRenderState);
    deviceTable[64] = reinterpret_cast<void*>(getTexture); deviceTable[65] = reinterpret_cast<void*>(setTexture);
    deviceTable[68] = reinterpret_cast<void*>(getSampler); deviceTable[69] = reinterpret_cast<void*>(setSampler);
    deviceTable[75] = reinterpret_cast<void*>(setScissor); deviceTable[76] = reinterpret_cast<void*>(getScissor);
    deviceTable[83] = reinterpret_cast<void*>(draw);
    deviceTable[87] = reinterpret_cast<void*>(setDeclaration); deviceTable[88] = reinterpret_cast<void*>(getDeclaration);
    deviceTable[89] = reinterpret_cast<void*>(setFVF); deviceTable[90] = reinterpret_cast<void*>(getFVF);
    deviceTable[92] = reinterpret_cast<void*>(setVertexShader); deviceTable[93] = reinterpret_cast<void*>(getVertexShader);
    deviceTable[100] = reinterpret_cast<void*>(setStream); deviceTable[101] = reinterpret_cast<void*>(getStream);
    deviceTable[102] = reinterpret_cast<void*>(setFrequency); deviceTable[103] = reinterpret_cast<void*>(getFrequency);
    deviceTable[106] = reinterpret_cast<void*>(createShader);
    deviceTable[107] = reinterpret_cast<void*>(setPixelShader); deviceTable[108] = reinterpret_cast<void*>(getPixelShader);
    deviceTable[109] = reinterpret_cast<void*>(setConstants); deviceTable[110] = reinterpret_cast<void*>(getConstants);
}
bool sameState(const FakeDevice& current, const FakeDevice& original) {
    return !memcmp(current.targets, original.targets, sizeof(current.targets)) && current.depth == original.depth &&
        !memcmp(current.textures, original.textures, sizeof(current.textures)) && current.pixelShader == original.pixelShader &&
        current.vertexShader == original.vertexShader && current.declaration == original.declaration &&
        current.stream == original.stream && current.offset == original.offset && current.stride == original.stride &&
        current.frequency == original.frequency && current.fvf == original.fvf &&
        !memcmp(&current.viewport, &original.viewport, sizeof(current.viewport)) &&
        !memcmp(&current.scissor, &original.scissor, sizeof(current.scissor)) &&
        !memcmp(current.renderStates, original.renderStates, sizeof(current.renderStates)) &&
        !memcmp(current.samplerStates, original.samplerStates, sizeof(current.samplerStates)) &&
        !memcmp(current.constants, original.constants, sizeof(current.constants));
}
post_fx::CameraSample camera() {
    post_fx::CameraSample sample;
    for (unsigned index = 0; index < 4; ++index) sample.view.elements[index][index] = sample.projection.elements[index][index] = 1;
    sample.camera = 1; sample.scene = 2; sample.depth = 3; sample.valid = true;
    return sample;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    initializeTables();
    const HMODULE module = LoadLibraryA(argv[1]);
    test::check("Compiled shaders load from the DLL resources", module != nullptr);
    if (!module) return 3;
    post_fx::PostProcess effects;
    test::check("All six shaders have valid ps_3_0 bytecode", effects.initialize(module));
    Object objects[12];
    for (auto& object : objects) object.table = objectTable;
    Object color{objectTable}, depth{objectTable};
    color.surface = &objects[10]; depth.surface = &objects[11];
    color.description.Width = depth.description.Width = 320;
    color.description.Height = depth.description.Height = 240;
    color.description.Format = D3DFMT_A8R8G8B8; depth.description.Format = D3DFMT_R32F;
    FakeDevice fake{deviceTable};
    for (unsigned index = 0; index < 4; ++index) fake.targets[index] = &objects[index];
    fake.depth = &objects[4]; fake.textures[0] = &objects[5]; fake.textures[1] = &objects[6];
    fake.textures[2] = &objects[6];
    fake.vertexShader = &objects[7]; fake.pixelShader = &objects[8]; fake.declaration = &objects[9]; fake.stream = &objects[5];
    for (const auto& state : post_fx::passStates) fake.renderStates[state.state] = 1;
    fake.renderStates[D3DRS_COLORWRITEENABLE1] = 0xf;
    for (unsigned sampler = 0; sampler < post_fx::effectSamplerCount; ++sampler)
        for (auto state : post_fx::samplerStates) fake.samplerStates[sampler][state] = sampler + 5;
    for (unsigned index = 0; index < std::size(fake.constants); ++index) fake.constants[index] = index + 0.25f;
    const FakeDevice original = fake;
    auto* device = reinterpret_cast<IDirect3DDevice9*>(&fake);
    auto* input = reinterpret_cast<IDirect3DTexture9*>(&color);
    auto* depthInput = reinterpret_cast<IDirect3DTexture9*>(&depth);
    post_fx::Settings settings;
    test::check("Both effects disabled perform no GPU calls", effects.render(device, input, depthInput, camera(), 1, settings) == S_FALSE && fake.creates == 0);
    settings.change(post_fx::antialiasing, 1);
    test::check("FXAA and copy passes succeed", SUCCEEDED(effects.render(device, input, depthInput, camera(), 1, settings)) && fake.draws == 2);
    test::check("Every changed device state is restored", sameState(fake, original));
    test::check("No pass reads from its output texture", !fake.alias);
    test::check("No pass overwrites the requested MRT depth mask", fake.forbiddenMasks == 0);
    const unsigned created = fake.creates;
    effects.render(device, input, depthInput, camera(), 1.033, settings);
    test::check("Steady-state drawing reuses all GPU resources", fake.creates == created);
    setRenderState(&fake, D3DRS_ALPHABLENDENABLE, TRUE);
    setRenderState(&fake, D3DRS_ZWRITEENABLE, FALSE);
    const FakeDevice translucent = fake;
    effects.render(device, input, depthInput, camera(), 1.066, settings);
    test::check("Translucent depth-mask tracking and effective mask are restored", sameState(fake, translucent) &&
        fake.blending && !fake.zWriting && fake.requestedMask == 0xf && fake.forbiddenMasks == 0);
    setRenderState(&fake, D3DRS_ZWRITEENABLE, TRUE);
    test::check("Restored tracking permits the next opaque draw to write depth", fake.renderStates[D3DRS_COLORWRITEENABLE1] == 0xf);
    settings.change(post_fx::motionBlur, 1);
    effects.render(device, input, depthInput, camera(), 2, settings);
    const unsigned beforeBlur = fake.draws;
    effects.render(device, input, depthInput, camera(), 2.033, settings);
    test::check("Blur plus FXAA draws three independent passes", fake.draws == beforeBlur + 3 && sameState(fake, original));
    const unsigned beforeMissingDepth = fake.draws;
    effects.render(device, input, nullptr, camera(), 2.066, settings);
    test::check("Missing depth skips blur while retaining FXAA", fake.draws == beforeMissingDepth + 2);
    fake.failDraw = fake.draws + 1;
    const unsigned beforeFailure = fake.draws;
    test::check("An effect draw failure is returned", FAILED(effects.render(device, input, depthInput, camera(), 2.099, settings)));
    test::check("Failure before copy never writes the game color", fake.draws == beforeFailure + 1);
    test::check("Draw failure restores every touched state", sameState(fake, original));
    effects.render(device, input, depthInput, camera(), 2.132, settings);
    test::check("Suspended effects do not retry each frame", fake.draws == beforeFailure + 1);
    effects.releaseResources();
    fake.failDraw = 0;
    test::check("Reset releases all created resources", [&]() {
        for (const auto* object : fake.created) if (object->references != 0) return false;
        return true;
    }());
    test::check("Restored game COM objects retain their original references", [&]() {
        for (const auto& object : objects) if (object.references != 1) return false;
        return color.references == 1 && depth.references == 1;
    }());
    effects.render(device, input, depthInput, camera(), 3, settings);
    test::check("Drawing resumes after Reset", !effects.failed() && sameState(fake, original));
    fake.failCapture = true;
    const unsigned beforeCaptureFailure = fake.draws;
    test::check("Capture failure skips all drawing", FAILED(effects.render(device, input, depthInput, camera(), 3.033, settings)) && fake.draws == beforeCaptureFailure);
    fake.failCapture = false;
    effects.releaseResources();
    fake.lost = true;
    test::check("Lost device performs no drawing", effects.render(device, input, depthInput, camera(), 4, settings) == D3DERR_DEVICELOST && fake.draws == beforeCaptureFailure);
    fake.lost = false;
    fake.failCreate = true;
    test::check("Shader creation failure is reported", FAILED(effects.render(device, input, depthInput, camera(), 4.033, settings)));
    const unsigned afterCreateFailure = fake.creates;
    effects.render(device, input, depthInput, camera(), 4.066, settings);
    test::check("Creation failure is not retried each frame", fake.creates == afterCreateFailure);
    fake.failCreate = false;
    color.description.Width = depth.description.Width = 640;
    effects.render(device, input, depthInput, camera(), 4.099, settings);
    test::check("Resolution changes recreate resources and resume", !effects.failed() && fake.creates > afterCreateFailure && sameState(fake, original));
    settings.change(post_fx::motionBlur, 0);
    settings.change(post_fx::antialiasing, post_fx::aaSmaa);
    const unsigned beforeSmaa = fake.draws;
    test::check("SMAA draws edges, weights, neighborhood and copy", SUCCEEDED(effects.render(device, input, nullptr, {}, 5, settings)) &&
        fake.draws == beforeSmaa + 4 && fake.clears == 1);
    test::check("SMAA restores all three sampler slots and other device state", sameState(fake, original) && !fake.alias);
    const unsigned beforeSmaaReuse = fake.creates;
    effects.render(device, input, nullptr, {}, 5.033, settings);
    test::check("SMAA reuses targets/lookups and clears stale edges every draw", fake.creates == beforeSmaaReuse && fake.clears == 2);
    fake.failDraw = fake.draws + 2;
    const unsigned beforeWeightsFailure = fake.draws;
    test::check("Weight-pass failure skips neighborhood blending and game-color copy", FAILED(effects.render(device, input, nullptr, {}, 5.066, settings)) &&
        fake.draws == beforeWeightsFailure + 2 && sameState(fake, original));
    effects.releaseResources();
    fake.failDraw = 0;
    settings.change(post_fx::antialiasing, post_fx::aaOff);
    const unsigned beforeResolve = fake.draws;
    test::check("SSAA resolves and replicates even with other effects off", SUCCEEDED(effects.render(device, input, nullptr, {}, 6, settings, 2)) &&
        fake.draws == beforeResolve + 2 && sameState(fake, original));
    const unsigned beforeResolveReuse = fake.creates;
    effects.render(device, input, nullptr, {}, 6.033, settings, 2);
    test::check("SSAA steady-state resolve reuses resources", fake.creates == beforeResolveReuse);
    fake.failDraw = fake.draws + 1;
    const unsigned beforeResolveFailure = fake.draws;
    test::check("SSAA downsample failure skips game-color write and restores device state",
        FAILED(effects.render(device, input, nullptr, {}, 6.066, settings, 2)) && fake.draws == beforeResolveFailure + 1 && sameState(fake, original));
    effects.releaseResources();
    test::check("SMAA/SSAA resources and lookup COM references are all released", [&]() {
        for (const auto* object : fake.created) if (object->references != 0) return false;
        return true;
    }());
    test::check("Only documented fake-device methods were called", fake.unexpected == 0);
    effects.releaseResources();
    for (auto* object : fake.created) delete object;
    FreeLibrary(module);
    std::puts("Fake COM only: no window, GPU or game process was used.");
    return test::finish();
}

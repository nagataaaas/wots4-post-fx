#include "game_render.h"
#include <MinHook.h>
#include <cstdio>
#include <cstring>

namespace post_fx {
namespace {
// Supported renderer layout. These locations connect scene-copy, camera and Reset.
constexpr uintptr_t sceneGlobal = 0x9eb070, rendererGlobal = 0x9e0890;
constexpr uintptr_t frameEntry = 0x7187a0, cameraEntry = 0x6e5c10, copyEntry = 0x542d00, recreateEntry = 0x890e20;
// This creator sizes raw color, encoded depth and both postprocess targets together.
constexpr uintptr_t sceneCreateEntry = 0x541930;
constexpr unsigned mainFrameOffset = 0x74, copyStepOffset = 0x9c, sceneHolderOffset = 0x58;
constexpr unsigned pingPongOffset = 0x60, pingPongIndexOffset = 0x68;
constexpr unsigned colorOffset = 0x50, depthOffset = 0x54, textureRendererOffset = 0x24, d3dTextureOffset = 0x5c;
constexpr unsigned viewOffset = 0x7f0, projectionOffset = 0x830, positionOffset = 0x88, portOffset = 0x110;
constexpr unsigned renderStateOffset = 0x720, deviceOffset = 0x2238;
constexpr unsigned hookLengths[] = {6, 7, 7, 5, 8};
constexpr uintptr_t hookEntries[] = {frameEntry, cameraEntry, copyEntry, recreateEntry, sceneCreateEntry};
template<class Value> Value read(void* object, unsigned offset) {
    return *reinterpret_cast<const Value*>(static_cast<const unsigned char*>(object) + offset);
}
void* pointer(void* object, unsigned offset) { return object ? read<void*>(object, offset) : nullptr; }
IDirect3DDevice9* deviceFrom(void* renderer) {
    return static_cast<IDirect3DDevice9*>(pointer(pointer(renderer, renderStateOffset), deviceOffset));
}
IDirect3DTexture9* textureFrom(void* holder, unsigned offset) {
    return static_cast<IDirect3DTexture9*>(pointer(pointer(pointer(holder, offset), textureRendererOffset), d3dTextureOffset));
}
bool fullPort(void* camera) {
    const auto* port = reinterpret_cast<const float*>(static_cast<const unsigned char*>(camera) + portOffset);
    return std::abs(port[0]) < 0.0001f && std::abs(port[1] - 1) < 0.0001f &&
        std::abs(port[2] - 1) < 0.0001f && std::abs(port[3]) < 0.0001f;
}
}
GameRender* GameRender::active = nullptr;

bool GameRender::codeMatches() const {
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(reinterpret_cast<void*>(base), &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
        memory.RegionSize < sizeof(IMAGE_DOS_HEADER) || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x1000) return false;
    const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (image->Signature != IMAGE_NT_SIGNATURE || image->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        image->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        (image->OptionalHeader.SizeOfImage != 0x65b000 && image->OptionalHeader.SizeOfImage != 0x689000) ||
        image->FileHeader.TimeDateStamp != 0x683d42eb) return false;
    auto matches = [this](uintptr_t preferred, const unsigned char* bytes, size_t count) {
        return !std::memcmp(reinterpret_cast<const void*>(address(preferred)), bytes, count);
    };
    const unsigned char frame[] = {0x53, 0x8b, 0xd9, 0x8b, 0x43, 0x18};
    const unsigned char camera[] = {0x83, 0xec, 0x24, 0x8b, 0x44, 0x24, 0x28};
    const unsigned char copy[] = {0x55, 0x8b, 0xec, 0x57, 0x8b, 0x7d, 0x0c};
    const unsigned char reset[] = {0x83, 0xec, 0x24, 0x53, 0x55};
    const unsigned char create[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x6a, 0xff};
    const unsigned char rawSize[] = {0x8b, 0x4d, 0x08, 0x8b, 0x46, 0x58, 0x8b, 0x55, 0x0c};
    const unsigned char firstSize[] = {0x8b, 0x4d, 0x08, 0x8b, 0x46, 0x60, 0x8b, 0x55, 0x0c};
    const unsigned char secondSize[] = {0x8b, 0x4d, 0x08, 0x8b, 0x55, 0x0c, 0x8b, 0x46, 0x64};
    const unsigned char renderState[] = {0x8b, 0x81, 0x20, 0x07, 0, 0};
    const unsigned char device[] = {0x8b, 0x80, 0x38, 0x22, 0, 0};
    const unsigned char textureLink[] = {0x89, 0x71, 0x24}, textureStorage[] = {0x89, 0x43, 0x5c};
    const unsigned char view[] = {0x8d, 0xb0, 0xf0, 0x07, 0, 0}, projection[] = {0x8d, 0xb0, 0x30, 0x08, 0, 0};
    const unsigned char callbackPrefix[] = {0xc7, 0x46, 0x0c};
    return matches(frameEntry, frame, sizeof(frame)) && matches(cameraEntry, camera, sizeof(camera)) &&
        matches(copyEntry, copy, sizeof(copy)) && matches(recreateEntry, reset, sizeof(reset)) &&
        matches(sceneCreateEntry, create, sizeof(create)) && matches(0x541aaf, rawSize, sizeof(rawSize)) &&
        matches(0x541bd8, firstSize, sizeof(firstSize)) && matches(0x541d04, secondSize, sizeof(secondSize)) &&
        matches(0x88e0f0, renderState, sizeof(renderState)) && matches(0x88e109, device, sizeof(device)) &&
        matches(0x8a1fbd, textureLink, sizeof(textureLink)) && matches(0x8a1f52, textureStorage, sizeof(textureStorage)) &&
        matches(0x8b2c6e, view, sizeof(view)) && matches(0x8b2c82, projection, sizeof(projection)) &&
        matches(0x5428e0, callbackPrefix, sizeof(callbackPrefix)) &&
        *reinterpret_cast<const uintptr_t*>(address(0x5428e3)) == address(copyEntry) &&
        *reinterpret_cast<const uintptr_t*>(address(0x947690 + 0x16c)) == address(0x88d660) &&
        *reinterpret_cast<const uintptr_t*>(address(0x984ed4 + 0x24)) == address(0x4b6510);
}

bool GameRender::install(const wots4::ModContext& context, Settings& options, PostProcess& processing, LogFunction logger) {
    base = reinterpret_cast<uintptr_t>(context.game);
    host = context.host;
    module = context.module;
    settings = &options;
    effects = &processing;
    log = logger;
    startupSamplingFactor = options.value(supersampling) ? 2 : 1;
    if (!codeMatches()) { log("[ERROR] Unsupported game code or renderer layout; Post FX was not installed."); return false; }
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return false;
    clockFrequency = static_cast<double>(frequency.QuadPart);
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return false;
    active = this;
    void* detours[] = {reinterpret_cast<void*>(drawFrame), reinterpret_cast<void*>(setCamera),
        reinterpret_cast<void*>(copyScene), reinterpret_cast<void*>(recreate), reinterpret_cast<void*>(createScene)};
    void** originals[] = {reinterpret_cast<void**>(&originalFrame), reinterpret_cast<void**>(&originalCamera),
        reinterpret_cast<void**>(&originalCopy), reinterpret_cast<void**>(&originalRecreate),
        reinterpret_cast<void**>(&originalCreateScene)};
    for (unsigned index = 0; index < std::size(hooks); ++index) {
        void* target = reinterpret_cast<void*>(address(hookEntries[index]));
        const MH_STATUS created = MH_CreateHook(target, detours[index], originals[index]);
        if (created == MH_OK) hooks[index] = target;
        if (created == MH_OK && host->claim(module, reinterpret_cast<uintptr_t>(target), hookLengths[index])) continue;
        char message[256];
        sprintf_s(message, "[ERROR] Could not reserve/install render hook at 0x%08x (MinHook status %d); Post FX was refused.",
            static_cast<unsigned>(hookEntries[index]), static_cast<int>(created));
        log(message);
        remove();
        return false;
    }
    for (void* target : hooks) {
        if (MH_EnableHook(target) == MH_OK) continue;
        log("[ERROR] Could not enable a render hook; Post FX installation was rolled back.");
        remove();
        return false;
    }
    return true;
}
void GameRender::remove() {
    bool removed = true;
    for (auto& target : hooks) {
        if (!target) continue;
        if (MH_RemoveHook(target) != MH_OK) { removed = false; continue; }
        target = nullptr;
    }
    if (removed && host) host->releaseClaims(module);
    else if (log) log("[ERROR] A render hook could not be removed; its reservations are retained.");
    if (effects) effects->releaseResources();
}

void __fastcall GameRender::drawFrame(void* frame, void*) {
    auto& render = *active;
    void* scene = *reinterpret_cast<void**>(render.address(sceneGlobal));
    const bool outermost = render.frameDepth == 0;
    if (outermost) {
        render.sample = {};
        render.ambiguous = render.copied = false;
        render.mainFrame = frame == pointer(scene, mainFrameOffset) ? frame : nullptr;
    } else {
        // A nested/offscreen Frame can change camera data. Do not expose mixed views.
        render.ambiguous = true;
    }
    ++render.frameDepth;
    render.originalFrame(frame);
    --render.frameDepth;
    if (outermost) {
        if (!render.copied) render.effects->clearHistory();
        render.mainFrame = nullptr;
    }
}

void __fastcall GameRender::setCamera(void* renderer, void*, void* camera) {
    auto& render = *active;
    render.originalCamera(renderer, camera);
    if (render.mainFrame && render.frameDepth == 1 && render.settings->value(motionBlur) && !render.copied)
        render.captureCamera(renderer, camera);
}

void GameRender::captureCamera(void* renderer, void* camera) {
    if (!camera || !renderer) return;
    void* scene = *reinterpret_cast<void**>(address(sceneGlobal));
    auto* device = deviceFrom(renderer);
    auto* holder = pointer(scene, sceneHolderOffset);
    auto* color = textureFrom(holder, colorOffset);
    auto* depth = textureFrom(holder, depthOffset);
    if (!device || !color || !depth) return;
    IDirect3DSurface9* currentColor = nullptr;
    IDirect3DSurface9* currentDepth = nullptr;
    IDirect3DSurface9* sceneColor = nullptr;
    IDirect3DSurface9* sceneDepth = nullptr;
    const bool matches = SUCCEEDED(device->GetRenderTarget(0, &currentColor)) &&
        SUCCEEDED(device->GetRenderTarget(1, &currentDepth)) && SUCCEEDED(color->GetSurfaceLevel(0, &sceneColor)) &&
        SUCCEEDED(depth->GetSurfaceLevel(0, &sceneDepth)) && currentColor == sceneColor && currentDepth == sceneDepth;
    release(currentColor); release(currentDepth); release(sceneColor); release(sceneDepth);
    if (!matches) return;
    if (!fullPort(camera)) { ambiguous = true; return; }
    CameraSample current;
    current.view = read<Matrix>(renderer, viewOffset);
    current.projection = read<Matrix>(renderer, projectionOffset);
    current.camera = reinterpret_cast<uintptr_t>(camera);
    current.scene = reinterpret_cast<uintptr_t>(scene);
    current.depth = reinterpret_cast<uintptr_t>(depth);
    for (unsigned axis = 0; axis < 3; ++axis) current.position[axis] = read<float>(camera, positionOffset + axis * 4);
    current.valid = finite(current.view) && finite(current.projection);
    if (sample.valid && (!matricesMatch(sample.view, current.view) || !matricesMatch(sample.projection, current.projection))) ambiguous = true;
    sample = current;
}

bool __cdecl GameRender::copyScene(void* step, void* scene) {
    auto& render = *active;
    if (render.mainFrame && render.frameDepth == 1 && scene && step == pointer(scene, copyStepOffset) &&
        render.mainFrame == pointer(scene, mainFrameOffset)) {
        if (!render.copied) {
            render.copied = true;
            render.process(scene);
        } else {
            render.effects->clearHistory();
        }
    }
    return render.originalCopy(step, scene);
}

void GameRender::process(void* scene) {
    const unsigned factor = scene == enlargedScene ? sceneSize.factor : 1;
    if (!settings->enabled() && factor == 1) { effects->clearHistory(); return; }
    const int index = read<signed char>(scene, pingPongIndexOffset);
    if (index < 0 || index > 1) { effects->clearHistory(); return; }
    void* renderer = *reinterpret_cast<void**>(address(rendererGlobal));
    auto* device = deviceFrom(renderer);
    auto* color = textureFrom(pointer(scene, pingPongOffset + index * 4), colorOffset);
    auto* depth = textureFrom(pointer(scene, sceneHolderOffset), depthOffset);
    D3DSURFACE_DESC description{};
    if (factor == 2 && (!color || FAILED(color->GetLevelDesc(0, &description)) ||
        description.Width != sceneSize.width || description.Height != sceneSize.height)) {
        samplingWarningPending = !samplingWarningReported;
        effects->clearHistory();
        return;
    }
    CameraSample current = sample;
    current.valid = current.valid && !ambiguous && current.scene == reinterpret_cast<uintptr_t>(scene);
    if (settings->value(motionBlur) && !current.valid && !cameraWarningReported) cameraWarningPending = true;
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) { effects->clearHistory(); return; }
    if (device) {
        D3DDEVICE_CREATION_PARAMETERS creation{};
        if (SUCCEEDED(device->GetCreationParameters(&creation)) && creation.hFocusWindow &&
            (IsIconic(creation.hFocusWindow) || GetForegroundWindow() != creation.hFocusWindow)) {
            effects->clearHistory();
            return;
        }
    }
    const HRESULT result = effects->render(device, color, depth, current,
        static_cast<double>(now.QuadPart) / clockFrequency, *settings, factor);
    if (FAILED(result) && !errorReported) pendingError = result;
}
void __fastcall GameRender::createScene(void* scene, void*, UINT width, UINT height) {
    auto& render = *active;
    SceneSize size{width, height, 1};
    if (render.startupSamplingFactor == 2) {
        void* renderer = *reinterpret_cast<void**>(render.address(rendererGlobal));
        auto* device = deviceFrom(renderer);
        D3DCAPS9 caps{};
        if (device && SUCCEEDED(device->GetDeviceCaps(&caps)))
            size = supersampledSize(width, height, 2, caps.MaxTextureWidth, caps.MaxTextureHeight);
        if (size.factor != 2) render.samplingWarningPending = !render.samplingWarningReported;
    }
    render.effects->releaseResources();
    render.originalCreateScene(scene, size.width, size.height);
    render.enlargedScene = scene;
    render.sceneSize = size;
}

bool __fastcall GameRender::recreate(void* renderer, void*) {
    auto& render = *active;
    render.effects->releaseResources();
    render.sample = {};
    render.errorReported = false;
    render.pendingError = S_OK;
    return render.originalRecreate(renderer);
}

void GameRender::report() {
    if (FAILED(pendingError) && !errorReported) {
        char message[256];
        sprintf_s(message, "[WARN] Post FX rendering failed (HRESULT 0x%08lx); effects are skipped. "
            "Suspended passes retry after device Reset or a target size/format change.", static_cast<unsigned long>(pendingError));
        log(message);
        errorReported = true;
    }
    if (cameraWarningPending && !cameraWarningReported) {
        log("[WARN] A complete single-camera depth image was unavailable; motion blur skips that draw and anti-aliasing remains independent.");
        cameraWarningReported = true;
        cameraWarningPending = false;
    }
    if (samplingWarningPending && !samplingWarningReported) {
        log("[WARN] SSAA target size exceeds device/allocation limits or differs from the expected scene size; "
            "unsupported creation uses native resolution and mismatched resolve passes are skipped.");
        samplingWarningReported = true;
        samplingWarningPending = false;
    }
}
}

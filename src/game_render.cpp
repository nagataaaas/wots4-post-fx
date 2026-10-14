#include "game_render.h"
#include <MinHook.h>
#include <cstdio>
#include <cstring>

namespace post_fx {
namespace {
// Addresses are at the exe's preferred base and belong to the supported Steam build.
constexpr DWORD supportedTimestamp = 0x683d42eb;
constexpr DWORD steamImageSize = 0x689000, decryptedImageSize = 0x65b000;  // the Steam image and its decrypted copy
constexpr LONG maximumHeaderOffset = 0x1000;

// Globals holding the scene manager and the DX9 renderer.
constexpr uintptr_t sceneGlobal = 0x9eb070, rendererGlobal = 0x9e0890;
// Hooked functions.
constexpr uintptr_t frameEntry = 0x7187a0;        // Draws one Frame (the main scene or an offscreen one)
constexpr uintptr_t cameraEntry = 0x6e5c10;       // Sets a camera's matrices on the renderer
constexpr uintptr_t copyEntry = 0x542d00;         // Frame step that copies the processed scene for display
constexpr uintptr_t recreateEntry = 0x890e20;     // Releases and recreates device resources around Reset
constexpr uintptr_t sceneCreateEntry = 0x541930;  // Sizes raw color, encoded depth and both postprocess targets together

// Scene manager.
constexpr unsigned sceneHolderOffset = 0x58, pingPongOffset = 0x60, pingPongIndexOffset = 0x68;
constexpr unsigned mainFrameOffset = 0x74, copyStepOffset = 0x9c;
// Render target holders, and the texture to D3D texture chain.
constexpr unsigned colorOffset = 0x50, depthOffset = 0x54, textureRendererOffset = 0x24, d3dTextureOffset = 0x5c;
// Renderer and camera.
constexpr unsigned renderStateOffset = 0x720, deviceOffset = 0x2238;
constexpr unsigned viewOffset = 0x7f0, projectionOffset = 0x830;
constexpr unsigned positionOffset = 0x88, portOffset = 0x110;

// The instructions that read or write the layout above, so a different build is refused.
constexpr unsigned char frameBytes[] = {0x53, 0x8b, 0xd9, 0x8b, 0x43, 0x18};
constexpr unsigned char cameraBytes[] = {0x83, 0xec, 0x24, 0x8b, 0x44, 0x24, 0x28};
constexpr unsigned char copyBytes[] = {0x55, 0x8b, 0xec, 0x57, 0x8b, 0x7d, 0x0c};
constexpr unsigned char recreateBytes[] = {0x83, 0xec, 0x24, 0x53, 0x55};
constexpr unsigned char sceneCreateBytes[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x6a, 0xff};
constexpr unsigned char rawSizeBytes[] = {0x8b, 0x4d, 0x08, 0x8b, 0x46, 0x58, 0x8b, 0x55, 0x0c};       // width/height to holder +0x58
constexpr unsigned char firstPingPongBytes[] = {0x8b, 0x4d, 0x08, 0x8b, 0x46, 0x60, 0x8b, 0x55, 0x0c};  // to +0x60
constexpr unsigned char secondPingPongBytes[] = {0x8b, 0x4d, 0x08, 0x8b, 0x55, 0x0c, 0x8b, 0x46, 0x64}; // to +0x64
constexpr unsigned char renderStateBytes[] = {0x8b, 0x81, 0x20, 0x07, 0, 0};   // mov eax, [ecx + 0x720]
constexpr unsigned char deviceBytes[] = {0x8b, 0x80, 0x38, 0x22, 0, 0};        // mov eax, [eax + 0x2238]
constexpr unsigned char textureLinkBytes[] = {0x89, 0x71, 0x24};               // mov [ecx + 0x24], esi
constexpr unsigned char textureStorageBytes[] = {0x89, 0x43, 0x5c};            // mov [ebx + 0x5c], eax
constexpr unsigned char viewBytes[] = {0x8d, 0xb0, 0xf0, 0x07, 0, 0};          // lea esi, [eax + 0x7f0]
constexpr unsigned char projectionBytes[] = {0x8d, 0xb0, 0x30, 0x08, 0, 0};    // lea esi, [eax + 0x830]
constexpr unsigned char stepCallbackBytes[] = {0xc7, 0x46, 0x0c};              // mov [esi + 0xc], imm32

struct CodeSignature { uintptr_t address; const unsigned char* bytes; size_t length; };
template<size_t Length> constexpr CodeSignature signature(uintptr_t address, const unsigned char (&bytes)[Length]) {
    return {address, bytes, Length};
}
constexpr CodeSignature codeSignatures[] = {
    signature(frameEntry, frameBytes), signature(cameraEntry, cameraBytes), signature(copyEntry, copyBytes),
    signature(recreateEntry, recreateBytes), signature(sceneCreateEntry, sceneCreateBytes),
    signature(0x541aaf, rawSizeBytes), signature(0x541bd8, firstPingPongBytes), signature(0x541d04, secondPingPongBytes),
    signature(0x88e0f0, renderStateBytes), signature(0x88e109, deviceBytes),
    signature(0x8a1fbd, textureLinkBytes), signature(0x8a1f52, textureStorageBytes),
    signature(0x8b2c6e, viewBytes), signature(0x8b2c82, projectionBytes),
    signature(0x5428e0, stepCallbackBytes),
};
// Pointers that must lead to the functions whose layout is checked above.
struct PointerSignature { uintptr_t slot; uintptr_t target; };
constexpr PointerSignature pointerSignatures[] = {
    {0x5428e3, copyEntry},          // the scene copy registered as a Frame step callback
    {0x947690 + 0x16c, 0x88d660},   // DX9 renderer vtable: builds the view/projection matrices
    {0x984ed4 + 0x24, 0x4b6510},    // postprocess step vtable: draws the enabled effects in order
};

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
    constexpr float tolerance = 0.0001f;
    const auto* port = reinterpret_cast<const float*>(static_cast<const unsigned char*>(camera) + portOffset);
    return std::abs(port[0]) < tolerance && std::abs(port[1] - 1) < tolerance &&
        std::abs(port[2] - 1) < tolerance && std::abs(port[3]) < tolerance;
}
bool windowFocused(IDirect3DDevice9* device) {
    D3DDEVICE_CREATION_PARAMETERS creation{};
    if (!device || FAILED(device->GetCreationParameters(&creation)) || !creation.hFocusWindow) return true;
    return !IsIconic(creation.hFocusWindow) && GetForegroundWindow() == creation.hFocusWindow;
}
}
GameRender* GameRender::active = nullptr;

bool GameRender::codeMatches() const {
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(reinterpret_cast<void*>(base), &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
        memory.RegionSize < sizeof(IMAGE_DOS_HEADER) || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > maximumHeaderOffset) return false;
    const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (image->Signature != IMAGE_NT_SIGNATURE || image->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        image->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        (image->OptionalHeader.SizeOfImage != steamImageSize && image->OptionalHeader.SizeOfImage != decryptedImageSize) ||
        image->FileHeader.TimeDateStamp != supportedTimestamp) return false;
    for (const auto& code : codeSignatures)
        if (std::memcmp(reinterpret_cast<const void*>(address(code.address)), code.bytes, code.length)) return false;
    for (const auto& link : pointerSignatures)
        if (*reinterpret_cast<const uintptr_t*>(address(link.slot)) != address(link.target)) return false;
    return true;
}

bool GameRender::install(const wots4::ModContext& context, Settings& options, PostProcess& processing, LogFunction logger) {
    base = reinterpret_cast<uintptr_t>(context.game);
    host = context.host;
    module = context.module;
    settings = &options;
    effects = &processing;
    log = logger;
    startupSamplingFactor = options.value(supersampling) ? 2 : 1;
    if (!codeMatches()) {
        log("[ERROR] Unsupported game code or renderer layout; Post FX was not installed.");
        return false;
    }
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
        log("[ERROR] The high-resolution timer needed for motion blur is unavailable; Post FX was not installed.");
        return false;
    }
    clockFrequency = static_cast<double>(frequency.QuadPart);
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        char message[128];
        sprintf_s(message, "[ERROR] MinHook could not be initialized (status %d); Post FX was not installed.", static_cast<int>(status));
        log(message);
        return false;
    }
    active = this;
    return installHooks();
}

bool GameRender::installHooks() {
    // Lengths cover the whole instructions that MinHook relocates at each entry.
    struct Hook { uintptr_t entry; unsigned length; void* detour; void** original; };
    const Hook entries[hookCount] = {
        {frameEntry, 6, reinterpret_cast<void*>(drawFrame), reinterpret_cast<void**>(&originalFrame)},
        {cameraEntry, 7, reinterpret_cast<void*>(setCamera), reinterpret_cast<void**>(&originalCamera)},
        {copyEntry, 7, reinterpret_cast<void*>(copyScene), reinterpret_cast<void**>(&originalCopy)},
        {recreateEntry, 5, reinterpret_cast<void*>(recreate), reinterpret_cast<void**>(&originalRecreate)},
        {sceneCreateEntry, 8, reinterpret_cast<void*>(createScene), reinterpret_cast<void**>(&originalCreateScene)},
    };
    for (unsigned index = 0; index < hookCount; ++index) {
        const Hook& hook = entries[index];
        void* target = reinterpret_cast<void*>(address(hook.entry));
        const MH_STATUS created = MH_CreateHook(target, hook.detour, hook.original);
        if (created == MH_OK) hooks[index] = target;
        if (created == MH_OK && host->claim(module, reinterpret_cast<uintptr_t>(target), hook.length)) continue;
        char message[256];
        sprintf_s(message, "[ERROR] Could not reserve/install render hook at 0x%08x (MinHook status %d); Post FX was refused.",
            static_cast<unsigned>(hook.entry), static_cast<int>(created));
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
        if (MH_RemoveHook(target) != MH_OK) {
            removed = false;
            continue;
        }
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
    release(currentColor);
    release(currentDepth);
    release(sceneColor);
    release(sceneDepth);
    if (!matches) return;
    if (!fullPort(camera)) {
        ambiguous = true;
        return;
    }
    CameraSample current;
    current.view = read<Matrix>(renderer, viewOffset);
    current.projection = read<Matrix>(renderer, projectionOffset);
    current.camera = reinterpret_cast<uintptr_t>(camera);
    current.scene = reinterpret_cast<uintptr_t>(scene);
    current.depth = reinterpret_cast<uintptr_t>(depth);
    for (unsigned axis = 0; axis < 3; ++axis) current.position[axis] = read<float>(camera, positionOffset + axis * 4);
    current.valid = finite(current.view) && finite(current.projection);
    const bool viewChanged = !matricesMatch(sample.view, current.view) || !matricesMatch(sample.projection, current.projection);
    if (sample.valid && viewChanged) ambiguous = true;
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
    if (!settings->enabled() && factor == 1) {
        effects->clearHistory();
        return;
    }
    const int index = read<signed char>(scene, pingPongIndexOffset);
    if (index < 0 || index > 1) {
        effects->clearHistory();
        return;
    }
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
    if (!QueryPerformanceCounter(&now) || !windowFocused(device)) {
        effects->clearHistory();
        return;
    }
    const double seconds = static_cast<double>(now.QuadPart) / clockFrequency;
    const HRESULT result = effects->render(device, color, depth, current, seconds, *settings, factor);
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
        if (pendingError == stateCaptureFailed) {
            log("[ERROR] Post FX could not read the device state it must restore; "
                "all passes are skipped until the device is reset or the resolution changes.");
        } else {
            char message[256];
            sprintf_s(message, "[WARN] A Post FX pass failed (HRESULT 0x%08lx); the failed pass is skipped "
                "until the device is reset or the resolution changes.", static_cast<unsigned long>(pendingError));
            log(message);
        }
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

#pragma once
#include "post_process.h"
#include "supersampling.h"
#include <wots4/mod_api.h>

namespace post_fx {
class GameRender {
public:
    bool install(const wots4::ModContext& context, Settings& settings, PostProcess& effects, LogFunction log);
    void report();
    void remove();
private:
    static void __fastcall drawFrame(void* frame, void*);
    static void __fastcall setCamera(void* renderer, void*, void* camera);
    static bool __cdecl copyScene(void* step, void* scene);
    static bool __fastcall recreate(void* renderer, void*);
    static void __fastcall createScene(void* scene, void*, UINT width, UINT height);
    void captureCamera(void* renderer, void* camera);
    void process(void* scene);
    bool codeMatches() const;
    uintptr_t address(uintptr_t preferred) const { return base + preferred - 0x400000; }
    static GameRender* active;
    uintptr_t base = 0;
    const wots4::HostApi* host = nullptr;
    HMODULE module = nullptr;
    Settings* settings = nullptr;
    PostProcess* effects = nullptr;
    LogFunction log = nullptr;
    void* hooks[5]{};
    using FrameFunction = void (__thiscall*)(void*);
    using CameraFunction = void (__thiscall*)(void*, void*);
    using CopyFunction = bool (__cdecl*)(void*, void*);
    using RecreateFunction = bool (__thiscall*)(void*);
    using CreateSceneFunction = void (__thiscall*)(void*, UINT, UINT);
    FrameFunction originalFrame = nullptr;
    CameraFunction originalCamera = nullptr;
    CopyFunction originalCopy = nullptr;
    RecreateFunction originalRecreate = nullptr;
    CreateSceneFunction originalCreateScene = nullptr;
    unsigned startupSamplingFactor = 1;
    void* enlargedScene = nullptr;
    SceneSize sceneSize{};
    bool samplingWarningPending = false, samplingWarningReported = false;
    void* mainFrame = nullptr;
    CameraSample sample;
    unsigned frameDepth = 0;
    bool copied = false, ambiguous = false;
    HRESULT pendingError = S_OK;
    bool errorReported = false, cameraWarningPending = false, cameraWarningReported = false;
    double clockFrequency = 1;
};
}

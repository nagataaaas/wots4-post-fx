#include "mapped_exe.h"
#include <filesystem>
#include <d3d9.h>

namespace {
using namespace mapped_exe;
unsigned claimCalls = 0, options = 0, frameCallbacks = 0, saveCallbacks = 0;
unsigned conflictAt = 0;
bool rejectPage = false;
wots4::OptionsPageCallbacks page{};
BOOL WINAPI claim(HMODULE owner, uintptr_t address, uint32_t length) {
    ++claimCalls;
    if (conflictAt && claimCalls == conflictAt) return FALSE;
    return hostClaim(owner, address, length);
}
BOOL WINAPI addPage(HMODULE, const wots4::LocalizedText* title, const wots4::OptionItem*, uint32_t count,
    const wots4::OptionsPageCallbacks* callbacks) {
    options = count;
    page = *callbacks;
    check("The registered page has both language titles", !wcscmp(title->japanese, L"映像後処理") && !wcscmp(title->english, L"Post FX"));
    return !rejectPage;
}
BOOL WINAPI onFrame(HMODULE, wots4::GameEventCallback, void*) { ++frameCallbacks; return TRUE; }
BOOL WINAPI onSave(HMODULE, wots4::GameEventCallback, void*) { ++saveCallbacks; return TRUE; }
void put(uintptr_t preferred, std::initializer_list<unsigned char> bytes) {
    memcpy(reinterpret_cast<void*>(va(static_cast<unsigned>(preferred))), bytes.begin(), bytes.size());
}
HRESULT STDMETHODCALLTYPE deviceCaps(void*, D3DCAPS9* caps) {
    *caps = {};
    caps->MaxTextureWidth = caps->MaxTextureHeight = 8192;
    return S_OK;
}
}
int main(int argc, char** argv) {
    if (argc < 3 || !mapped_exe::mapImage(argv[1])) return 2;
    using namespace mapped_exe;
    const std::string scenario = argc > 3 ? argv[3] : "valid";
    conflictAt = scenario == "conflict" ? 2 : scenario == "scene_conflict" ? 5 : 0;
    rejectPage = scenario == "registration";
    // Preserve the guarded prologues; replace only their bodies with inert returns.
    // No original frame, camera, D3D, import, entry point or TLS code is executed.
    put(0x7187a6, {0x5b, 0xc3});
    put(0x6e5c17, {0x83, 0xc4, 0x24, 0xc2, 0x04, 0x00});
    put(0x542d07, {0xb0, 0x01, 0x5f, 0x5d, 0xc3});
    put(0x890e25, {0x5d, 0x5b, 0x83, 0xc4, 0x24, 0xb0, 0x01, 0xc3});
    // Record creator arguments in the synthetic scene; never execute resource creation.
    put(0x541938, {0x8b, 0x45, 0x08, 0x89, 0x41, 0x08, 0x8b, 0x45, 0x0c, 0x89, 0x41, 0x0c,
        0x89, 0xec, 0x5d, 0xc2, 0x08, 0x00});
    if (scenario == "unsupported") *reinterpret_cast<unsigned char*>(va(0x7187a0)) = 0x90;
    if (scenario == "layout") *reinterpret_cast<unsigned char*>(va(0x541d04)) = 0x90;
    if (scenario == "steam") {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(imageBase);
        auto* image = reinterpret_cast<IMAGE_NT_HEADERS32*>(imageBase + dos->e_lfanew);
        image->OptionalHeader.SizeOfImage = 0x689000;
    }
    const auto fixtureDirectory = std::filesystem::absolute(argv[0]).parent_path() / ("post_fx_native_" + scenario);
    std::filesystem::create_directories(fixtureDirectory);
    const auto config = fixtureDirectory / "config.ini";
    std::filesystem::remove(config);
    if (scenario == "ssaa") WritePrivateProfileStringW(L"antialiasing", L"supersampling", L"2x", config.c_str());
    const auto directory = fixtureDirectory.wstring() + L"\\";
    const auto module = LoadLibraryA(argv[2]);
    const auto initialize = module ? reinterpret_cast<wots4::Initialize>(GetProcAddress(module, "Wots4ModInitialize")) : nullptr;
    check("The x86 DLL exports Wots4ModInitialize", initialize != nullptr);
    if (!initialize) return 3;
    wots4::HostApi host{sizeof(host), wots4::modApiVersion, hostLog, claim, hostRelease, addPage, onFrame, onSave};
    wots4::ModContext context{sizeof(context), wots4::modApiVersion, nullptr, module,
        reinterpret_cast<HMODULE>(imageBase), directory.c_str(), &host};
    check("Null context is refused", initialize(nullptr) == FALSE);
    auto unsupported = context;
    unsupported.version = 99;
    check("Unknown API version is refused", initialize(&unsupported) == FALSE);
    const BOOL accepted = initialize(&context);
    if (scenario == "valid" || scenario == "steam" || scenario == "ssaa") {
        check("Known renderer initializes without a device", accepted == TRUE);
        check("Five options and save/frame callbacks register", options == 5 && frameCallbacks == 1 && saveCallbacks == 1);
        check("All overwritten hook ranges are reserved", claimed(0x7187a0, 6) && claimed(0x6e5c10, 7) &&
            claimed(0x542d00, 7) && claimed(0x890e20, 5) && claimed(0x541930, 8));
        uint32_t values[5]{};
        page.read(page.context, values, 5);
        check("Options initially disable effects", values[0] == 0 && values[1] == 0 && values[2] == 1 && values[3] == 1);
        check("SSAA loads its startup selection", values[4] == (scenario == "ssaa" ? 1u : 0u));
        unsigned char scene[0xc8]{}, frame[0x30]{};
        *reinterpret_cast<void**>(va(0x9eb070)) = scene;
        *reinterpret_cast<void**>(scene + 0x74) = frame;
        volatile auto draw = reinterpret_cast<void (__thiscall*)(void*)>(va(0x7187a0));
        draw(frame);
        check("Frame hook forwards to its inert original", true);
        volatile auto camera = reinterpret_cast<void (__thiscall*)(void*, void*)>(va(0x6e5c10));
        camera(scene, frame);
        check("Camera hook preserves thiscall stack cleanup", true);
        volatile auto copy = reinterpret_cast<bool (__cdecl*)(void*, void*)>(va(0x542d00));
        check("Scene callback forwards outside the main frame", copy(nullptr, scene));
        volatile auto reset = reinterpret_cast<bool (__thiscall*)(void*)>(va(0x890e20));
        check("Recreate hook releases Mod resources and forwards", reset(scene));
        volatile auto create = reinterpret_cast<void (__thiscall*)(void*, UINT, UINT)>(va(0x541930));
        void* deviceTable[8]{};
        deviceTable[7] = reinterpret_cast<void*>(deviceCaps);
        void** device = deviceTable;
        unsigned char renderer[0x724]{}, renderState[0x223c]{};
        *reinterpret_cast<void**>(va(0x9e0890)) = renderer;
        *reinterpret_cast<void**>(renderer + 0x720) = renderState;
        *reinterpret_cast<void**>(renderState + 0x2238) = &device;
        create(scene, 640, 480);
        const UINT factor = scenario == "ssaa" ? 2 : 1;
        check("Scene creation forwards scaled dimensions and preserves thiscall cleanup", *reinterpret_cast<UINT*>(scene + 8) == 640 * factor &&
            *reinterpret_cast<UINT*>(scene + 12) == 480 * factor);
        page.changed(page.context, 4, scenario == "ssaa" ? 0 : 1);
        create(scene, 800, 600);
        check("SSAA changes wait until restart", *reinterpret_cast<UINT*>(scene + 8) == 800 * factor &&
            *reinterpret_cast<UINT*>(scene + 12) == 600 * factor);
        create(scene, 6000, 4000);
        check("Oversized SSAA creation falls back to native dimensions", *reinterpret_cast<UINT*>(scene + 8) == 6000 &&
            *reinterpret_cast<UINT*>(scene + 12) == 4000);
    } else {
        check("Invalid/overlapping/rejected initialization is refused", accepted == FALSE);
        check("Refusal leaves no hook reservations", claims.empty());
        if (conflictAt) {
            check("A partial installation releases its claims", releases == 1 && claimCalls == conflictAt);
            check("A partial installation restores entry bytes", *reinterpret_cast<unsigned char*>(va(0x7187a0)) == 0x53 &&
                *reinterpret_cast<unsigned char*>(va(0x6e5c10)) == 0x83 &&
                *reinterpret_cast<unsigned char*>(va(0x541930)) == 0x55);
        } else {
            check("Refusal never claims game code", claimCalls == 0);
        }
    }
    printf("RESULT: %u checks passed, %u failed\n", tests - failures, failures);
    puts("Only a mapped executable copy with inert function bodies was used; no game was started.");
    puts("No writes to the installation, saves or game process.");
    std::filesystem::remove(config);
    std::filesystem::remove(fixtureDirectory);
    return failures ? 1 : 0;
}

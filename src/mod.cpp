#include "game_render.h"
#include <cstddef>

namespace {
const wots4::HostApi* host = nullptr;
HMODULE module = nullptr;
post_fx::Settings settings;
post_fx::PostProcess effects;
post_fx::GameRender rendering;
void log(const char* message) { if (host && host->log) host->log(module, message); }
void WINAPI save(void*) { settings.save(); }
void WINAPI report(void*) { rendering.report(); }
bool supported(const wots4::ModContext* context) {
    if (!context || context->size < sizeof(*context) || context->version != wots4::modApiVersion ||
        !context->game || !context->directory || !context->host || !context->module) return false;
    const auto& api = *context->host;
    constexpr size_t requiredSize = offsetof(wots4::HostApi, onOptionsSaved) + sizeof(api.onOptionsSaved);
    return api.size >= requiredSize && api.version == wots4::modApiVersion && api.claim && api.releaseClaims &&
        api.addOptionsPage && api.onFrame && api.onOptionsSaved;
}
}
extern "C" BOOL WINAPI ModInitialize(const wots4::ModContext* context) {
    if (!supported(context)) return FALSE;
    host = context->host;
    module = context->module;
    try {
        settings.initialize(context->directory, log);
        if (!effects.initialize(module)) { log("[ERROR] Embedded shader bytecode is missing or invalid; Post FX was refused."); return FALSE; }
        if (!settings.registerPage(*context) || !host->onOptionsSaved(module, save, nullptr) ||
            !host->onFrame(module, report, nullptr)) {
            log("[ERROR] The ModLoader did not accept the Post FX options/callbacks; Post FX was refused.");
            return FALSE;
        }
        if (!rendering.install(*context, settings, effects, log)) return FALSE;
        log("[INFO] Post FX initialized: five options registered; FXAA/SMAA, motion blur and restart-applied SSAA are selectable.");
        return TRUE;
    } catch (...) {
        rendering.remove();
        log("[ERROR] Post FX initialization failed while preparing its settings; the installation was rolled back.");
        return FALSE;
    }
}

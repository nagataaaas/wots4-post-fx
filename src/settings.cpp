#include "settings.h"
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <iterator>

namespace post_fx {
namespace {
const wots4::LocalizedText aaChoices[] = {{L"無効", L"Off"}, {L"FXAA", L"FXAA"}, {L"SMAA", L"SMAA"}};
const wots4::LocalizedText ssaaChoices[] = {{L"無効", L"Off"}, {L"縦横2倍（4サンプル）", L"2x per axis (4 samples)"}};
const wots4::LocalizedText toggleChoices[] = {{L"有効", L"On"}, {L"無効", L"Off"}};
const wots4::LocalizedText strengths[] = {{L"弱", L"Low"}, {L"標準", L"Normal"}, {L"強", L"High"}};
const wots4::LocalizedText qualities[] = {{L"低", L"Low"}, {L"標準", L"Normal"}, {L"高", L"High"}};
const wots4::OptionItem items[] = {
    {wots4::optionChoice, {L"アンチエイリアス", L"Anti-Aliasing"}, aaChoices, 3, 0},
    {wots4::optionToggle, {L"モーションブラー", L"Motion Blur"}, toggleChoices, 2, 0},
    {wots4::optionChoice, {L"ブラーの強さ", L"Blur Strength"}, strengths, 3, 1},
    {wots4::optionChoice, {L"ブラーの品質", L"Blur Quality"}, qualities, 3, 1},
    {wots4::optionChoice, {L"SSAA", L"SSAA"}, ssaaChoices, 2, 0},
};
// config.ini spells each choice by name; the array index is the option value.
// enabled keeps 0/1 like every Mod's on/off key (a toggle's value 1 is "on").
const wchar_t* const methodNames[] = {L"off", L"fxaa", L"smaa"};
const wchar_t* const enabledNames[] = {L"0", L"1"};
const wchar_t* const levelNames[] = {L"low", L"normal", L"high"};
const wchar_t* const supersamplingNames[] = {L"off", L"2x"};
struct ConfigKey { const wchar_t* section; const wchar_t* key; const wchar_t* const* names; };
const ConfigKey configKeys[] = {
    {L"antialiasing", L"method", methodNames},
    {L"motion_blur", L"enabled", enabledNames},
    {L"motion_blur", L"strength", levelNames},
    {L"motion_blur", L"quality", levelNames},
    {L"antialiasing", L"supersampling", supersamplingNames},
};
static_assert(std::size(configKeys) == optionCount && std::size(items) == optionCount);
// Shown under SSAA while it differs from the value the game started with, which is the one in use.
const wots4::LocalizedText restartNotice{
    L"SSAAの変更はゲームを再起動した後に有効になります", L"Changes to SSAA will take effect after restarting the game"};
constexpr uint32_t pageItemCount = optionCount + 1;
// Longer than any choice name, so a truncated read is never mistaken for a valid one.
constexpr DWORD valueBufferLength = 16;

bool findChoice(const ConfigKey& config, uint32_t choiceCount, const wchar_t* text, uint32_t& value) {
    for (uint32_t choice = 0; choice < choiceCount; ++choice) {
        if (_wcsicmp(text, config.names[choice])) continue;
        value = choice;
        return true;
    }
    return false;
}
std::wstring acceptedNames(const ConfigKey& config, uint32_t choiceCount) {
    std::wstring names;
    for (uint32_t choice = 0; choice < choiceCount; ++choice) {
        if (choice) names += L", ";
        names += config.names[choice];
    }
    return names;
}
}

const wots4::OptionItem* Settings::definitions() { return items; }

void Settings::initialize(const wchar_t* directory, LogFunction logger) {
    path = std::wstring(directory) + L"config.ini";
    log = logger;
    dirty = false;
    for (uint32_t item = 0; item < optionCount; ++item) {
        const ConfigKey& config = configKeys[item];
        const uint32_t choiceCount = items[item].choiceCount;
        values[item] = items[item].defaultValue;
        wchar_t text[valueBufferLength];
        const DWORD length = GetPrivateProfileStringW(config.section, config.key, L"", text, valueBufferLength, path.c_str());
        if (!length) continue;
        const bool truncated = length >= valueBufferLength - 1;
        if (!truncated && findChoice(config, choiceCount, text, values[item])) continue;
        if (!log) continue;
        char message[256];
        sprintf_s(message, "[WARN] config.ini [%ls] %ls=%ls%ls is not one of %ls; using the default %ls.",
            config.section, config.key, text, truncated ? L"..." : L"", acceptedNames(config, choiceCount).c_str(),
            config.names[items[item].defaultValue]);
        log(message);
    }
    startupSupersampling = values[supersampling];
    invalidateHistory();
}

uint32_t Settings::value(uint32_t item) const { return item < optionCount ? values[item] : 0; }

bool Settings::change(uint32_t item, uint32_t number) {
    if (item >= optionCount || number >= items[item].choiceCount) return false;
    if (values[item] == number) return true;
    values[item] = number;
    dirty = true;
    invalidateHistory();
    return true;
}

// The page has the restart notice after the options, unless the ModLoader refused it.
void WINAPI Settings::readPage(void* context, uint32_t* output, uint32_t count) {
    if (!output || (count != optionCount && count != pageItemCount)) return;
    auto& settings = *static_cast<Settings*>(context);
    for (uint32_t item = 0; item < optionCount; ++item) output[item] = settings.values[item];
    // Opening the options interrupts exposure history even when no value changes.
    settings.invalidateHistory();
}

void WINAPI Settings::changePage(void* context, uint32_t item, uint32_t number) {
    static_cast<Settings*>(context)->change(item, number);
}

bool Settings::registerPage(const wots4::ModContext& context) {
    const wots4::LocalizedText title{L"映像後処理", L"Post FX"};
    const wots4::OptionsPageCallbacks callbacks{this, readPage, changePage};
    wots4::OptionItem page[pageItemCount];
    std::copy(std::begin(items), std::end(items), page);
    const uint32_t otherSupersampling = startupSupersampling ? 0 : 1;
    page[optionCount] = wots4::optionNoticeItem(restartNotice, supersampling, {otherSupersampling});
    if (context.host->addOptionsPage(context.module, &title, page, pageItemCount, &callbacks)) return true;
    if (log) log("[WARN] The ModLoader did not accept the options page with the SSAA restart notice; "
        "adding it without the notice (a ModLoader without notices refuses them).");
    return context.host->addOptionsPage(context.module, &title, items, optionCount, &callbacks) != FALSE;
}

void Settings::save() {
    if (!dirty) return;
    for (uint32_t item = 0; item < optionCount; ++item) {
        const ConfigKey& config = configKeys[item];
        // Updates the key in place, keeping the comments of the bundled config.ini.
        if (WritePrivateProfileStringW(config.section, config.key, config.names[values[item]], path.c_str())) continue;
        if (log) {
            char message[256];
            sprintf_s(message, "[ERROR] Could not save config.ini [%ls] %ls (Windows error %lu); "
                "settings remain active and will be retried on the next options save.",
                config.section, config.key, GetLastError());
            log(message);
        }
        return;
    }
    // A cache-flush call returns zero even on success; only the writes above
    // determine whether the settings need to be retried.
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    dirty = false;
}
}

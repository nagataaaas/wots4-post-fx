#include "settings.h"
#include <cstdio>
#include <cwchar>
#include <cerrno>

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
    {wots4::optionChoice, {L"SSAA（保存後に再起動）", L"SSAA (save and restart)"}, ssaaChoices, 2, 0},
};
const wchar_t* sections[] = {L"antialiasing", L"motion_blur", L"motion_blur", L"motion_blur", L"antialiasing"};
const wchar_t* keys[] = {L"method", L"enabled", L"strength", L"quality", L"supersampling"};
}

const wots4::OptionItem* Settings::definitions() { return items; }

void Settings::initialize(const wchar_t* directory, LogFunction logger) {
    path = std::wstring(directory) + L"config.ini";
    log = logger;
    dirty = false;
    for (uint32_t item = 0; item < optionCount; ++item) {
        values[item] = items[item].defaultValue;
        wchar_t text[32];
        const DWORD length = GetPrivateProfileStringW(sections[item], keys[item], L"", text, _countof(text), path.c_str());
        if (!length) continue;
        wchar_t* end = nullptr;
        errno = 0;
        const unsigned long number = std::wcstoul(text, &end, 10);
        if (length < _countof(text) - 1 && text[0] >= L'0' && text[0] <= L'9' && end != text && *end == L'\0' && !errno &&
            number < items[item].choiceCount) {
            values[item] = number;
        } else if (log) {
            char message[256];
            sprintf_s(message, "[WARN] Invalid config.ini [%ls] %ls; using default value %u.",
                sections[item], keys[item], items[item].defaultValue);
            log(message);
        }
    }
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

void WINAPI Settings::readPage(void* context, uint32_t* output, uint32_t count) {
    if (!output || count != optionCount) return;
    auto& settings = *static_cast<Settings*>(context);
    for (uint32_t item = 0; item < count; ++item) output[item] = settings.values[item];
    // Opening the options interrupts exposure history even when no value changes.
    settings.invalidateHistory();
}

void WINAPI Settings::changePage(void* context, uint32_t item, uint32_t number) {
    static_cast<Settings*>(context)->change(item, number);
}

bool Settings::registerPage(const wots4::ModContext& context) {
    const wots4::LocalizedText title{L"映像後処理", L"Post FX"};
    const wots4::OptionsPageCallbacks callbacks{this, readPage, changePage};
    return context.host->addOptionsPage(context.module, &title, items, optionCount, &callbacks) != FALSE;
}

void Settings::save() {
    if (!dirty) return;
    for (uint32_t item = 0; item < optionCount; ++item) {
        wchar_t text[16];
        swprintf_s(text, L"%u", values[item]);
        if (WritePrivateProfileStringW(sections[item], keys[item], text, path.c_str())) continue;
        if (log) {
            char message[256];
            sprintf_s(message, "[ERROR] Could not save config.ini [%ls] %ls (Windows error %lu); "
                "settings remain active and will be retried on the next options save.",
                sections[item], keys[item], GetLastError());
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

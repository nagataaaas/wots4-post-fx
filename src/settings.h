#pragma once
#include <wots4/mod_api.h>
#include <string>

namespace post_fx {
constexpr uint32_t optionCount = 5;
enum OptionIndex : uint32_t { antialiasing, motionBlur, blurStrength, blurQuality, supersampling };
enum AntialiasingMethod : uint32_t { aaOff, aaFxaa, aaSmaa };
using LogFunction = void (*)(const char*);

class Settings {
public:
    void initialize(const wchar_t* directory, LogFunction logger);
    bool registerPage(const wots4::ModContext& context);
    void save();
    bool change(uint32_t item, uint32_t value);
    uint32_t value(uint32_t item) const;
    uint32_t revision() const { return generation; }
    bool enabled() const { return values[antialiasing] || values[motionBlur]; }
    void invalidateHistory() { ++generation; }
    static const wots4::OptionItem* definitions();
private:
    static void WINAPI readPage(void*, uint32_t*, uint32_t);
    static void WINAPI changePage(void*, uint32_t, uint32_t);
    uint32_t values[optionCount] = {0, 0, 1, 1, 0};
    uint32_t generation = 0;
    bool dirty = false;
    std::wstring path;
    LogFunction log = nullptr;
};
}

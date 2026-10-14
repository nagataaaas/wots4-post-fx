#include "settings.h"
#include "supersampling.h"
#include "test_support.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
wots4::OptionsPageCallbacks callbacks{};
unsigned warnings = 0;
void log(const char* message) { ++warnings; std::printf("  log: %s\n", message); }
std::wstring readValue(const std::filesystem::path& config, const wchar_t* section, const wchar_t* key) {
    wchar_t text[64]{};
    GetPrivateProfileStringW(section, key, L"", text, static_cast<DWORD>(std::size(text)), config.c_str());
    return text;
}
// The last page the fake ModLoader accepted. refuseNotices acts like a ModLoader without notices.
std::wstring addedJapaneseTitle, addedEnglishTitle;
std::vector<wots4::OptionItem> addedItems;  // Their texts are the Mod's static strings
bool refuseNotices = false;
BOOL WINAPI addPage(HMODULE, const wots4::LocalizedText* title, const wots4::OptionItem* items,
    uint32_t count, const wots4::OptionsPageCallbacks* registered) {
    for (uint32_t item = 0; item < count && refuseNotices; ++item)
        if (items[item].kind == wots4::optionNotice) return FALSE;
    addedJapaneseTitle = title->japanese;
    addedEnglishTitle = title->english;
    addedItems.assign(items, items + count);
    callbacks = *registered;
    return TRUE;
}
void checkRegisteredPage() {
    test::check("Japanese and English page titles", addedJapaneseTitle == L"映像後処理" && addedEnglishTitle == L"Post FX");
    test::check("One page with six options and the SSAA notice", addedItems.size() == post_fx::optionCount + 1);
    if (addedItems.size() != post_fx::optionCount + 1) return;
    const auto& items = addedItems;
    test::check("AA choices include SMAA and SSAA has Off, 1.5x and 2x",
        items[post_fx::antialiasing].choiceCount == 3 && !wcscmp(items[post_fx::antialiasing].choices[2].english, L"SMAA") &&
        items[post_fx::supersampling].choiceCount == 3 && !wcscmp(items[post_fx::supersampling].label.english, L"SSAA") &&
        !wcscmp(items[post_fx::supersampling].choices[1].english, L"1.5x per axis (2.25 samples)"));
    test::check("Sharpening follows anti-aliasing with Off and three strengths",
        items[post_fx::sharpening].choiceCount == 4 && !wcscmp(items[post_fx::sharpening].label.japanese, L"シャープ化") &&
        !wcscmp(items[post_fx::sharpening].choices[3].english, L"High") && items[post_fx::sharpening].defaultValue == 0);
    test::check("Motion blur uses the toggle ABI", items[post_fx::motionBlur].kind == wots4::optionToggle &&
        !wcscmp(items[post_fx::motionBlur].choices[0].english, L"On") && !wcscmp(items[post_fx::motionBlur].choices[1].english, L"Off"));
    // Started with SSAA off, so the notice shows while SSAA is 1.5x or 2x (bits 1 and 2).
    test::check("The notice under SSAA shows while SSAA differs from the startup value",
        items[post_fx::optionCount].kind == wots4::optionNotice && items[post_fx::optionCount].defaultValue == post_fx::supersampling &&
        items[post_fx::optionCount].choiceCount == 0b110 && !wcscmp(items[post_fx::optionCount].label.japanese, L"SSAAの変更はゲームを再起動した後に有効になります"));
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path bundledConfig = argv[1];
    const auto directory = std::filesystem::absolute(argv[0]).parent_path() / "post_fx_settings_fixture";
    std::filesystem::create_directories(directory);
    const auto config = directory / "config.ini";
    std::filesystem::remove(config);
    const std::wstring prefix = directory.wstring() + L"\\";
    post_fx::Settings settings;
    settings.initialize(prefix.c_str(), log);
    test::check("Defaults disable every effect and retain normal blur settings", !settings.enabled() &&
        settings.value(post_fx::sharpening) == post_fx::sharpeningOff &&
        settings.value(post_fx::blurStrength) == 1 && settings.value(post_fx::blurQuality) == 1);
    test::check("Initialization does not generate a config file", !std::filesystem::exists(config));
    wots4::HostApi host{};
    host.addOptionsPage = addPage;
    wots4::ModContext context{};
    context.host = &host;
    test::check("Settings page registers", settings.registerPage(context));
    checkRegisteredPage();
    uint32_t values[post_fx::optionCount + 1]{};
    const auto firstRevision = settings.revision();
    callbacks.read(callbacks.context, values, post_fx::optionCount + 1);
    test::check("Opening options invalidates history", settings.revision() != firstRevision);
    test::check("Page reads all six values", values[post_fx::antialiasing] == 0 && values[post_fx::sharpening] == 0 &&
        values[post_fx::motionBlur] == 0 && values[post_fx::blurStrength] == 1 && values[post_fx::blurQuality] == 1 &&
        values[post_fx::supersampling] == 0);

    refuseNotices = true;
    post_fx::Settings withoutNotices;
    withoutNotices.initialize(prefix.c_str(), log);
    const unsigned warningsBeforeRefusal = warnings;
    test::check("A ModLoader without notices gets the page without the SSAA notice",
        withoutNotices.registerPage(context) && addedItems.size() == post_fx::optionCount &&
        warnings == warningsBeforeRefusal + 1);
    uint32_t optionValues[post_fx::optionCount]{7, 7, 7, 7, 7, 7};
    callbacks.read(callbacks.context, optionValues, post_fx::optionCount);
    test::check("That page reads its six values", optionValues[post_fx::blurStrength] == 1 && optionValues[post_fx::supersampling] == 0);
    refuseNotices = false;
    warnings = warningsBeforeRefusal;
    test::check("Settings page registers again", settings.registerPage(context));
    test::check("Out-of-range item and choice are rejected", !settings.change(post_fx::optionCount, 0) &&
        !settings.change(post_fx::antialiasing, 3) && !settings.change(post_fx::sharpening, 4) && !settings.change(post_fx::supersampling, 3));
    callbacks.changed(callbacks.context, post_fx::motionBlur, 1);
    callbacks.changed(callbacks.context, post_fx::antialiasing, 1);
    test::check("Toggle value 1 and FXAA index 1 enable independently",
        settings.value(post_fx::motionBlur) == 1 && settings.value(post_fx::antialiasing) == 1);
    test::check("Changing a value does not save immediately", !std::filesystem::exists(config));
    settings.save();
    test::check("Explicit options save generates config.ini", std::filesystem::exists(config));
    post_fx::Settings restored;
    restored.initialize(prefix.c_str(), log);
    test::check("INI roundtrip retains enabled effects", restored.value(post_fx::antialiasing) == 1 && restored.value(post_fx::motionBlur) == 1);
    restored.change(post_fx::motionBlur, 0);
    test::check("AA remains enabled when blur is off", restored.enabled() && restored.value(post_fx::motionBlur) == 0);
    restored.change(post_fx::antialiasing, 0);
    test::check("Every effect off bypasses processing", !restored.enabled());
    restored.change(post_fx::sharpening, post_fx::sharpeningNormal);
    test::check("Sharpening alone enables processing", restored.enabled());
    restored.change(post_fx::sharpening, post_fx::sharpeningOff);
    test::check("config.ini stores choice names", readValue(config, L"antialiasing", L"method") == L"fxaa" &&
        readValue(config, L"motion_blur", L"enabled") == L"1" && readValue(config, L"motion_blur", L"strength") == L"normal");
    WritePrivateProfileStringW(L"antialiasing", L"method", L"SMAA", config.c_str());
    restored.initialize(prefix.c_str(), log);
    test::check("Choice names are case-insensitive", restored.value(post_fx::antialiasing) == post_fx::aaSmaa && warnings == 0);
    for (const wchar_t* invalid : {L"1", L"2", L"on", L"fxaa2", L"smaa smaa"}) {
        WritePrivateProfileStringW(L"antialiasing", L"method", invalid, config.c_str());
        restored.initialize(prefix.c_str(), log);
        test::check("Numbers and unknown names use the default", restored.value(post_fx::antialiasing) == 0);
    }
    test::check("Invalid values produce readable warnings", warnings == 5);
    settings.change(post_fx::antialiasing, post_fx::aaSmaa);
    settings.change(post_fx::supersampling, post_fx::supersamplingDouble);
    settings.save();
    restored.initialize(prefix.c_str(), log);
    test::check("SMAA and SSAA roundtrip independently", restored.value(post_fx::antialiasing) == post_fx::aaSmaa &&
        restored.value(post_fx::supersampling) == post_fx::supersamplingDouble &&
        readValue(config, L"antialiasing", L"supersampling") == L"2x");
    settings.change(post_fx::supersampling, post_fx::supersamplingOneAndHalf);
    settings.save();
    restored.initialize(prefix.c_str(), log);
    test::check("SSAA 1.5x roundtrips by name", restored.value(post_fx::supersampling) == post_fx::supersamplingOneAndHalf &&
        readValue(config, L"antialiasing", L"supersampling") == L"1.5x");
    test::check("Started with 1.5x, the notice shows while SSAA is off or 2x", [&]() {
        if (!restored.registerPage(context)) return false;
        return addedItems.size() == post_fx::optionCount + 1 && addedItems[post_fx::optionCount].choiceCount == 0b101;
    }());
    settings.change(post_fx::sharpening, post_fx::sharpeningHigh);
    settings.save();
    restored.initialize(prefix.c_str(), log);
    test::check("Sharpening roundtrips by name in its own section", restored.value(post_fx::sharpening) == post_fx::sharpeningHigh &&
        readValue(config, L"sharpening", L"strength") == L"high");
    WritePrivateProfileStringW(L"antialiasing", L"method", L"3", config.c_str());
    WritePrivateProfileStringW(L"motion_blur", L"strength", L"highhighhighhighhighhighhighhigh", config.c_str());
    restored.initialize(prefix.c_str(), log);
    test::check("Truncated INI values cannot silently become a different valid choice", restored.value(post_fx::blurStrength) == 1);
    const auto missing = directory / "missing";
    const std::wstring missingPrefix = missing.wstring() + L"\\";
    post_fx::Settings retry;
    retry.initialize(missingPrefix.c_str(), log);
    retry.change(post_fx::antialiasing, 1);
    retry.save();
    test::check("Save failure is logged and leaves the active value intact", warnings == 8 && retry.value(post_fx::antialiasing) == 1);
    std::filesystem::create_directories(missing);
    retry.save();
    post_fx::Settings retried;
    retried.initialize(missingPrefix.c_str(), log);
    test::check("The next options save retries previously failed writes", retried.value(post_fx::antialiasing) == 1);
    std::filesystem::remove(missing / "config.ini");
    std::filesystem::remove(missing);

    std::filesystem::copy_file(bundledConfig, config, std::filesystem::copy_options::overwrite_existing);
    const unsigned warningsBeforeBundled = warnings;
    post_fx::Settings bundled;
    bundled.initialize(prefix.c_str(), log);
    test::check("The bundled config.ini holds the defaults without warnings", warnings == warningsBeforeBundled &&
        bundled.value(post_fx::antialiasing) == 0 && bundled.value(post_fx::sharpening) == 0 && bundled.value(post_fx::motionBlur) == 0 &&
        bundled.value(post_fx::blurStrength) == 1 && bundled.value(post_fx::blurQuality) == 1 && bundled.value(post_fx::supersampling) == 0);
    bundled.change(post_fx::antialiasing, post_fx::aaFxaa);
    bundled.save();
    test::check("Saving updates the bundled file in place and keeps its comments", [&]() {
        std::ifstream file(config);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return text.find("method=fxaa") != std::string::npos && text.find("; Edge smoothing") != std::string::npos;
    }());
    std::filesystem::remove(config);
    std::filesystem::remove(directory);
    return test::finish();
}

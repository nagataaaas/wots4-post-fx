#include "settings.h"
#include "test_support.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
wots4::OptionsPageCallbacks callbacks{};
unsigned warnings = 0;
void log(const char* message) { ++warnings; std::printf("  log: %s\n", message); }
std::wstring readValue(const std::filesystem::path& config, const wchar_t* section, const wchar_t* key) {
    wchar_t text[64]{};
    GetPrivateProfileStringW(section, key, L"", text, static_cast<DWORD>(std::size(text)), config.c_str());
    return text;
}
BOOL WINAPI addPage(HMODULE, const wots4::LocalizedText* title, const wots4::OptionItem* items,
    uint32_t count, const wots4::OptionsPageCallbacks* registered) {
    test::check("Japanese and English page titles", !wcscmp(title->japanese, L"映像後処理") && !wcscmp(title->english, L"Post FX"));
    test::check("One page with five options", count == post_fx::optionCount);
    test::check("AA choices include SMAA and SSAA explains its restart requirement",
        items[0].choiceCount == 3 && !wcscmp(items[0].choices[2].english, L"SMAA") &&
        items[4].choiceCount == 2 && wcsstr(items[4].label.english, L"restart"));
    test::check("Motion blur uses the toggle ABI", items[1].kind == wots4::optionToggle &&
        !wcscmp(items[1].choices[0].english, L"On") && !wcscmp(items[1].choices[1].english, L"Off"));
    callbacks = *registered;
    return TRUE;
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
    test::check("Defaults disable both effects and retain normal blur settings", !settings.enabled() &&
        settings.value(2) == 1 && settings.value(3) == 1);
    test::check("Initialization does not generate a config file", !std::filesystem::exists(config));
    wots4::HostApi host{};
    host.addOptionsPage = addPage;
    wots4::ModContext context{};
    context.host = &host;
    test::check("Settings page registers", settings.registerPage(context));
    uint32_t values[post_fx::optionCount]{};
    const auto firstRevision = settings.revision();
    callbacks.read(callbacks.context, values, post_fx::optionCount);
    test::check("Opening options invalidates history", settings.revision() != firstRevision);
    test::check("Page reads all five values", values[0] == 0 && values[1] == 0 && values[2] == 1 && values[3] == 1 && values[4] == 0);
    test::check("Out-of-range item and choice are rejected", !settings.change(5, 0) && !settings.change(0, 3) && !settings.change(4, 2));
    callbacks.changed(callbacks.context, 1, 1);
    callbacks.changed(callbacks.context, 0, 1);
    test::check("Toggle value 1 and FXAA index 1 enable independently", settings.value(1) == 1 && settings.value(0) == 1);
    test::check("Changing a value does not save immediately", !std::filesystem::exists(config));
    settings.save();
    test::check("Explicit options save generates config.ini", std::filesystem::exists(config));
    post_fx::Settings restored;
    restored.initialize(prefix.c_str(), log);
    test::check("INI roundtrip retains enabled effects", restored.value(0) == 1 && restored.value(1) == 1);
    restored.change(1, 0);
    test::check("AA remains enabled when blur is off", restored.enabled() && restored.value(1) == 0);
    restored.change(0, 0);
    test::check("Both off bypass processing", !restored.enabled());
    test::check("config.ini stores choice names", readValue(config, L"antialiasing", L"method") == L"fxaa" &&
        readValue(config, L"motion_blur", L"enabled") == L"1" && readValue(config, L"motion_blur", L"strength") == L"normal");
    WritePrivateProfileStringW(L"antialiasing", L"method", L"SMAA", config.c_str());
    restored.initialize(prefix.c_str(), log);
    test::check("Choice names are case-insensitive", restored.value(post_fx::antialiasing) == post_fx::aaSmaa && warnings == 0);
    for (const wchar_t* invalid : {L"1", L"2", L"on", L"fxaa2", L"smaa smaa"}) {
        WritePrivateProfileStringW(L"antialiasing", L"method", invalid, config.c_str());
        restored.initialize(prefix.c_str(), log);
        test::check("Numbers and unknown names use the default", restored.value(0) == 0);
    }
    test::check("Invalid values produce readable warnings", warnings == 5);
    settings.change(post_fx::antialiasing, post_fx::aaSmaa);
    settings.change(post_fx::supersampling, 1);
    settings.save();
    restored.initialize(prefix.c_str(), log);
    test::check("SMAA and SSAA roundtrip independently", restored.value(post_fx::antialiasing) == post_fx::aaSmaa &&
        restored.value(post_fx::supersampling) == 1 && readValue(config, L"antialiasing", L"supersampling") == L"2x");
    WritePrivateProfileStringW(L"antialiasing", L"method", L"3", config.c_str());
    WritePrivateProfileStringW(L"motion_blur", L"strength", L"highhighhighhighhighhighhighhigh", config.c_str());
    restored.initialize(prefix.c_str(), log);
    test::check("Truncated INI values cannot silently become a different valid choice", restored.value(2) == 1);
    const auto missing = directory / "missing";
    const std::wstring missingPrefix = missing.wstring() + L"\\";
    post_fx::Settings retry;
    retry.initialize(missingPrefix.c_str(), log);
    retry.change(0, 1);
    retry.save();
    test::check("Save failure is logged and leaves the active value intact", warnings == 8 && retry.value(0) == 1);
    std::filesystem::create_directories(missing);
    retry.save();
    post_fx::Settings retried;
    retried.initialize(missingPrefix.c_str(), log);
    test::check("The next options save retries previously failed writes", retried.value(0) == 1);
    std::filesystem::remove(missing / "config.ini");
    std::filesystem::remove(missing);

    std::filesystem::copy_file(bundledConfig, config, std::filesystem::copy_options::overwrite_existing);
    const unsigned warningsBeforeBundled = warnings;
    post_fx::Settings bundled;
    bundled.initialize(prefix.c_str(), log);
    test::check("The bundled config.ini holds the defaults without warnings", warnings == warningsBeforeBundled &&
        bundled.value(0) == 0 && bundled.value(1) == 0 && bundled.value(2) == 1 && bundled.value(3) == 1 && bundled.value(4) == 0);
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

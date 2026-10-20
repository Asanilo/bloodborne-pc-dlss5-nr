// SPDX-License-Identifier: GPL-2.0-or-later
// bbport_settings.cpp: the menu saves its keys into bbport.ini and keeps the launcher's (controls).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include "gpu/shim/bbport_settings.h"

static std::string Read(const char* path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

int main() {
    assert(!BbSettings::IsHotEffect(6)); // SSR resizes resources on restart.
    assert(BbSettings::IsHotEffect(7));  // Bloom still switches live.
    assert(!BbSettings::IsHotEffect(8)); // Startup intros.
    char path[] = "/tmp/bbport-settings-test-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0);
    const char ini[] = "# launcher\nupscaler=fsr3\nkey.cross=X, Space\npad.circle=a\nshow_fps=0\n"
                       "fullscreen_hint=1\n";
    assert(write(fd, ini, sizeof(ini) - 1) == ssize_t(sizeof(ini) - 1));
    close(fd);
    setenv("BB_CONFIG", path, 1);
    setenv("BB_MENU_LANGUAGE", "en", 1);

    BbSettings::Load();
    auto& s = BbSettings::Get();
    assert(s.upscaler == BbSettings::UpscalerFsr3 && !s.show_fps && s.menu_x < 0.0f);
    s.show_fps = true;
    s.upscaler = BbSettings::UpscalerFsr411;
    s.menu_x = 0.625f;
    s.menu_y = 0.125f;
    BbSettings::Save();

    const std::string saved = Read(path);
    // The launcher's controls, its other keys and comments stay; the menu's keys are replaced
    // in place, new ones appended.
    assert(saved.find("# launcher\nupscaler=fsr411\nkey.cross=X, Space\npad.circle=a\nshow_fps=1\n"
                      "fullscreen_hint=1\n") == 0);
    assert(saved.find("menu_pos=0.6250,0.1250\n") != std::string::npos);
    assert(saved.find("upscaler=fsr3") == std::string::npos);
    assert(saved.find("menu_language=") == std::string::npos);

    // Saving unrelated settings must not pin the launcher's inherited language (#134).
    setenv("BB_MENU_LANGUAGE", "zh_CN", 1);
    BbSettings::Load();
    assert(s.menu_language == BbSettings::MenuLanguage::Chinese && !s.menu_language_chosen);
    BbSettings::Save();
    assert(Read(path).find("menu_language=") == std::string::npos);
    setenv("BB_MENU_LANGUAGE", "ru", 1);
    BbSettings::Load();
    assert(s.menu_language == BbSettings::MenuLanguage::Russian && !s.menu_language_chosen);

    s.menu_x = -1.0f;
    s.menu_y = -1.0f;
    BbSettings::Load();
    assert(s.menu_x == 0.625f && s.menu_y == 0.125f && s.upscaler == BbSettings::UpscalerFsr411);
    // Both upstream's explicit Russian choice and the local Chinese choice must persist.
    s.menu_language = BbSettings::MenuLanguage::Russian;
    s.menu_language_chosen = true;
    BbSettings::Save();
    assert(Read(path).find("menu_language=ru\n") != std::string::npos);
    setenv("BB_MENU_LANGUAGE", "en", 1);
    s.menu_language_chosen = false;
    BbSettings::Load();
    assert(s.menu_language == BbSettings::MenuLanguage::Russian && s.menu_language_chosen);
    s.menu_language = BbSettings::MenuLanguage::Chinese;
    s.menu_language_chosen = true;
    BbSettings::Save();
    assert(Read(path).find("menu_language=zh_CN\n") != std::string::npos);
    setenv("BB_MENU_LANGUAGE", "en", 1);
    BbSettings::Load();
    assert(s.menu_language == BbSettings::MenuLanguage::Chinese && s.menu_language_chosen);
    assert(std::string(BbSettings::MenuText("Close", "unused")) == "关闭");
    setenv("BB_UPSCALER", "dlss", 1);
    BbSettings::Load();
    assert(s.upscaler == BbSettings::UpscalerDlss);
    BbSettings::ConfigureDlssSupport(true, nullptr);
    assert(s.upscaler == BbSettings::UpscalerDlss && s.dlss_supported);
    BbSettings::Save();
    assert(Read(path).find("upscaler=dlss\n") != std::string::npos);
    BbSettings::ConfigureDlssSupport(false, "test: bridge unavailable");
    assert(s.upscaler == BbSettings::UpscalerFsr3 && !s.dlss_supported);
    setenv("BB_UPSCALER", "xess", 1);
    BbSettings::Load();
    assert(s.upscaler == BbSettings::UpscalerFsr3);
    unsetenv("BB_UPSCALER");
    unsetenv("BB_MENU_LANGUAGE");
    unlink(path);
    std::puts("PASS: settings save keeps other keys, menu position, inherited/explicit menu language");
}

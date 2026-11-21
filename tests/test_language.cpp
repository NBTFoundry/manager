// SPDX-License-Identifier: MPL-2.0
/* The manager GUI's languages: texts and their placeholders, UTF-8 cuts,
 * the worker's messages translated, iDrive's language record, the Language
 * screen and Chinese and Japanese drawn by the renderer. */
#include "nbt/gui/car_language.hpp"
#include "nbt/gui/frame.hpp"
#include "nbt/gui/language_ui.hpp"
#include "nbt/gui/text.hpp"
#include "nbt/gui/ui_controller.hpp"
#include "nbt/gui/ui_renderer.hpp"
#include "nbt/gui/view_builder.hpp"
#include "nbt/gui/worker_text.hpp"
#include "nbt/model/language.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

using nbt::gui::Request;
using nbt::gui::UiController;
using nbt::gui::UiView;
using namespace nbt::model;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define MANAGER_SETTINGS                                                                           \
    "SETTINGS\t3\trestart\tdebug_log|Debug log|off|off,on\tinvert|Invert knob|off|off,on"          \
    "\trelease|Hand back controller|off|off,on"

static const uint64_t now = 1000;

/* Valid UTF-8, every sequence complete. */
static bool utf8Valid(const char *s) {
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
    while (*p) {
        const unsigned need = *p < 0x80 ? 0 : *p >= 0xf0 ? 3 : *p >= 0xe0 ? 2 : *p >= 0xc0 ? 1 : 9;
        if (need == 9) {
            return false;
        }
        for (unsigned i = 1; i <= need; ++i) {
            if ((p[i] & 0xc0) != 0x80) {
                return false;
            }
        }
        p += need + 1;
    }
    return true;
}

static void test_model(void) {
    int choice = 0;
    CHECK(!strcmp(languageCode(LangJa), "ja") && !languageCode(7) && !languageCode(-1));
    CHECK(!strcmp(languageName(LangDe), "Deutsch") && !strcmp(languageName(LangZh), "中文"));
    CHECK(languageSettingParse("auto", choice) && choice == kLanguageAuto);
    CHECK(languageSettingParse("es", choice) && choice == LangEs);
    CHECK(!languageSettingParse("nl", choice) && !languageSettingParse("EN", choice) &&
          !languageSettingParse("", choice) && !languageSettingParse(NULL, choice));
    CHECK(!strcmp(languageSettingCode(kLanguageAuto), "auto") &&
          !strcmp(languageSettingCode(LangFr), "fr"));
    /* iDrive's numbers (AAIdrive, from vehicle.language on the car). */
    const struct {
        unsigned value;
        int language;
    } cars[] = {{1, LangDe}, {2, LangEn},  {3, LangEn},  {4, LangEs},  {16, LangEs}, {5, LangIt},
                {6, LangFr}, {15, LangFr}, {10, LangZh}, {11, LangZh}, {13, LangJa}, {0, -1},
                {7, -1},     {8, -1},      {12, -1},     {14, -1},     {24, -1},     {255, -1}};
    for (size_t i = 0; i < sizeof(cars) / sizeof(cars[0]); ++i) {
        Language language = LangEn;
        const bool known = languageFromCar(cars[i].value, language);
        CHECK(known == (cars[i].language >= 0) && (!known || language == cars[i].language));
    }
    /* A chosen language wins; Automatic follows iDrive; Dutch or none: English. */
    CHECK(languageEffective(LangIt, LangJa) == LangIt);
    CHECK(languageEffective(kLanguageAuto, LangJa) == LangJa);
    CHECK(languageEffective(kLanguageAuto, -1) == LangEn);
    CHECK(languageEffective(42, 99) == LangEn);
}

static void test_texts(void) {
    using namespace nbt::gui;
    char out[64];
    CHECK(!strcmp(textIn(LangFr, T_SETTINGS), "Réglages") &&
          !strcmp(textIn(LangJa, T_SETTINGS), "設定"));
    CHECK(!strcmp(textIn(LangEn, static_cast<TextId>(kTextCount)), ""));
    setCurrentLanguage(LangDe);
    CHECK(currentLanguage() == LangDe && !strcmp(text(T_BACK), "Zurück"));
    volatile int notOne = 9;
    setCurrentLanguage(static_cast<Language>(notOne)); // not a language: unchanged
    CHECK(currentLanguage() == LangDe);
    textf(out, sizeof(out), T_INSTALLING_APP, "nav", "v2");
    CHECK(!strcmp(out, "nav v2 wird installiert"));
    setCurrentLanguage(LangJa);
    textf(out, sizeof(out), T_VERSIONS, "v1", "v2");
    CHECK(!strcmp(out, "インストール済み：v1、新規：v2"));
    textf(out, sizeof(out), T_APP_SETTINGS, NULL); // a missing argument is empty
    CHECK(!strcmp(out, " の設定"));
    /* Cut at a whole character: 3-byte characters into 8 bytes leave 6. */
    textf(out, 8, T_NO_APPS);
    CHECK(strlen(out) == 6 && utf8Valid(out) && !strncmp(out, "アプ", 6));
    textf(out, 1, T_NO_APPS);
    CHECK(out[0] == 0);
    /* An argument cut in the middle of a character. */
    textf(out, 6, T_STOP_APP, "über");
    CHECK(utf8Valid(out) && !strcmp(out, "über"));
    textf(out, 5, T_STOP_APP, "über");
    CHECK(utf8Valid(out) && !strcmp(out, "übe"));
    setCurrentLanguage(LangEn);
    ViewBuilder::formatText(out, 4, "%s", "aé"); // "a", then half of "é" fits
    CHECK(utf8Valid(out) && !strcmp(out, "aé"));
    ViewBuilder::formatText(out, 3, "%s", "aé");
    CHECK(!strcmp(out, "a"));
    char trimmed[] = "ok\xe4\xb8";
    utf8TrimEnd(trimmed);
    CHECK(!strcmp(trimmed, "ok"));
    char whole[] = "ok\xe4\xb8\xad";
    utf8TrimEnd(whole);
    CHECK(!strcmp(whole, "ok\xe4\xb8\xad"));
    char lead[] = "ok\xc3";
    utf8TrimEnd(lead);
    CHECK(!strcmp(lead, "ok"));
    /* Columns: CJK counts two. */
    CHECK(utf8Fit("abcdef", 64, 4) == 4 && utf8Fit("中文日本", 64, 5) == 6);
    CHECK(utf8Fit("中文", 4, 10) == 3 && utf8Fit("", 64, 4) == 0);
}

static void test_worker_texts(void) {
    using namespace nbt::gui;
    char out[128];
    setCurrentLanguage(LangFr);
    workerText(out, sizeof(out), "Application stopped");
    CHECK(!strcmp(out, "Application arrêtée"));
    workerText(out, sizeof(out), "Package rejected: signature invalid");
    CHECK(!strcmp(out, "Paquet refusé : signature invalid"));
    workerText(out, sizeof(out), "NBT Manager v3 starts when App Manager opens again");
    CHECK(!strcmp(out, "NBT Manager v3 démarre à la prochaine ouverture d'App Manager"));
    workerText(out, sizeof(out), "cannot stop candidate for recovery"); // not translated
    CHECK(!strcmp(out, "cannot stop candidate for recovery"));
    workerText(out, sizeof(out), "Package rejected: "); // {1} matches some text only
    CHECK(!strcmp(out, "Package rejected: "));
    workerText(out, sizeof(out), "");
    CHECK(!strcmp(out, ""));
    workerText(out, 10, "Session expired");
    CHECK(utf8Valid(out) && !strcmp(out, "Session e"));
    setCurrentLanguage(LangEn);
    workerText(out, sizeof(out), "Checking application health");
    CHECK(!strcmp(out, "Checking application health"));
}

static void test_car_record(void) {
    using namespace nbt::gui;
    unsigned value = 0;
    CHECK(carLanguageParse("{\"language\":13}", value) && value == 13);
    CHECK(carLanguageParse("{\"language\":0}", value) && value == 0);
    CHECK(carLanguageParse("{\"language\":255}", value) && value == 255);
    const char *bad[] = {
        "{\"language\":256}", "{\"language\":-1}", "{\"language\":013}", "{\"language\":}",
        "{\"language\":1} ",  "{\"language\":1",   "{\"lang\":1}",       "",
        "{\"language\":1000}"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(!carLanguageParse(bad[i], value));
    }
    CHECK(!carLanguageParse(NULL, value));
    char dir[] = "/tmp/nbt-language-XXXXXX";
    CHECK(mkdtemp(dir));
    char path[64], link[64];
    snprintf(path, sizeof(path), "%s/record", dir);
    snprintf(link, sizeof(link), "%s/link", dir);
    CHECK(!carLanguageRead(path, value)); // none yet
    CHECK(carLanguageWrite(path, 6) && carLanguageRead(path, value) && value == 6);
    CHECK(carLanguageWrite(path, 13) && carLanguageRead(path, value) && value == 13);
    CHECK(!carLanguageWrite(path, 256));
    struct stat st;
    CHECK(!stat(path, &st) && (st.st_mode & 0777) == 0600);
    /* Not through a link, not shared, not malformed. */
    CHECK(!symlink(path, link) && !carLanguageRead(link, value));
    CHECK(!chmod(path, 0644) && !carLanguageRead(path, value) && !chmod(path, 0600));
    const char *records[] = {"NBTLANG1\nvalue=13", "NBTLANG2\nvalue=13\n", "NBTLANG1\nvalue=x\n",
                             "NBTLANG1\nvalue=300\n", "NBTLANG1\nvalue=1\n\n"};
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        const int fd = open(path, O_WRONLY | O_TRUNC);
        CHECK(fd >= 0 &&
              write(fd, records[i], strlen(records[i])) ==
                  static_cast<ssize_t>(strlen(records[i])) &&
              !close(fd));
        CHECK(!carLanguageRead(path, value));
    }
    CHECK(!unlink(link) && !unlink(path) && !rmdir(dir));
}

/* Answers the background requests, then expects `expected` and answers it. */
static int answer(UiController &ui, const char *expected, const char *reply) {
    Request r;
    for (;;) {
        CHECK(ui.nextRequest(now, r) == 1);
        if (!strcmp(r.command, "PHONE STATUS")) {
            CHECK(ui.reply("PHONE\tnone", now) == 0);
        } else if (!strcmp(r.command, "SETUP")) {
            CHECK(ui.reply("SETUP\tcurrent", now) == 0);
        } else if (!strcmp(r.command, "STATUS") && strcmp(expected, "STATUS")) {
            CHECK(ui.reply("STATUS\t0\t-\t-\t-\t0\t-\tIdle", now) == 0);
        } else {
            break;
        }
    }
    if (strcmp(r.command, expected)) {
        fprintf(stderr, "got '%s' want '%s'\n", r.command, expected);
        exit(1);
    }
    const int result = ui.reply(reply, now);
    while (!result && (ui.queued == nbt::gui::R_PHONE_STATUS || ui.queued == nbt::gui::R_SETUP ||
                       (ui.queued == nbt::gui::R_STATUS && ui.screen == nbt::gui::Apps))) {
        CHECK(ui.nextRequest(now, r) == 1);
        CHECK(ui.reply(!strcmp(r.command, "PHONE STATUS") ? "PHONE\tnone"
                       : !strcmp(r.command, "SETUP")      ? "SETUP\tcurrent"
                                                          : "STATUS\t0\t-\t-\t-\t0\t-\tIdle",
                       now) == 0);
    }
    return result;
}

static UiView view(const UiController &ui) {
    UiView v;
    CHECK(ui.view(v).ok());
    return v;
}

#define PRESS(ui, key) CHECK((ui).press(key) == 1)

static void test_language_screen(void) {
    using namespace nbt::gui;
    UiController ui;
    CHECK(ui.init("lo", 0).ok());
    CHECK(answer(ui, "LIST", "APPS\t1\tdemo") == 0);
    CHECK(answer(ui, "RUNNING status", "RUNNING\t0") == 0);
    PRESS(ui, KeyDown);
    PRESS(ui, KeySelect);
    CHECK(answer(ui, "SETTINGS manager live", MANAGER_SETTINGS) == 0);
    UiView v = view(ui);
    CHECK(ui.screen == Settings && !strcmp(v.items[3], "Language: Automatic (English)"));
    /* iDrive in German: Automatic follows it at once, the manager's own
     * settings included; the values sent stay the declared ones. */
    ui.carLanguage = LangDe;
    ui.applyLanguage();
    v = view(ui);
    CHECK(!strcmp(v.title, "Einstellungen") && !strcmp(v.items[0], "Debug-Protokoll: aus") &&
          !strcmp(v.items[3], "Sprache: Automatisch (Deutsch)") &&
          !strcmp(v.items[4], "Telefon koppeln"));
    PRESS(ui, KeySelect); // Debug log's choices, translated
    v = view(ui);
    CHECK(ui.screen == Choice && !strcmp(v.title, "Debug-Protokoll") &&
          !strcmp(v.items[0], "aus  (aktuell)") && !strcmp(v.items[1], "an"));
    PRESS(ui, KeyDown);
    PRESS(ui, KeySelect);
    CHECK(answer(ui, "SET manager debug_log on", "OK") == 0);
    CHECK(answer(ui, "SETTINGS manager live", MANAGER_SETTINGS) == 0);
    /* The Language screen: Automatic with iDrive's language, then each language by its name. */
    while (ui.focus < ui.managerEntry(EntryLanguage)) {
        PRESS(ui, KeyDown);
    }
    PRESS(ui, KeySelect);
    v = view(ui);
    CHECK(ui.screen == LanguagePick && ui.focus == 0 && v.itemCount == 8 &&
          !strcmp(v.title, "Sprache") && !strcmp(v.items[0], "Automatisch (Deutsch)  (aktuell)") &&
          !strcmp(v.items[1], "English") && !strcmp(v.items[2], "Français") &&
          !strcmp(v.items[6], "中文") && !strcmp(v.items[7], "日本語"));
    CHECK(!strcmp(screenName(ui.screen), "language"));
    PRESS(ui, KeyBack);
    CHECK(ui.screen == Settings && ui.focus == ui.managerEntry(EntryLanguage));
    /* Français: saved, then shown, whatever iDrive's language. */
    PRESS(ui, KeySelect);
    PRESS(ui, KeyDown);
    PRESS(ui, KeyDown);
    PRESS(ui, KeySelect);
    CHECK(ui.press(KeyDown) == 1 && ui.press(KeySelect) == 0); // busy while it is saved
    CHECK(answer(ui, "SET manager language fr", "OK") == 0);
    CHECK(ui.screen == Settings && ui.focus == ui.managerEntry(EntryLanguage) &&
          ui.languageChoice == LangFr && currentLanguage() == LangFr);
    CHECK(!strcmp(view(ui).items[3], "Langue : Français"));
    ui.carLanguage = LangJa;
    ui.applyLanguage();
    CHECK(currentLanguage() == LangFr);
    /* A refused save says so and keeps the language. */
    PRESS(ui, KeySelect);
    CHECK(ui.focus == 2 && !strcmp(view(ui).items[2], "Français  (actuel)"));
    PRESS(ui, KeyUp);
    PRESS(ui, KeyUp);
    PRESS(ui, KeySelect);
    CHECK(answer(ui, "SET manager language auto", "ERR settings") == 0);
    CHECK(ui.screen == Settings && ui.languageChoice == LangFr &&
          !strcmp(view(ui).lines[0], "Impossible d'enregistrer le réglage"));
    /* Automatic saved: iDrive's Japanese. */
    PRESS(ui, KeySelect);
    PRESS(ui, KeyUp);
    PRESS(ui, KeyUp);
    PRESS(ui, KeySelect);
    CHECK(answer(ui, "SET manager language auto", "OK") == 0);
    CHECK(ui.languageChoice == kLanguageAuto && currentLanguage() == LangJa &&
          !strcmp(view(ui).title, "設定") && !strcmp(view(ui).items[3], "言語：自動（日本語）"));
    /* Dutch in iDrive: Automatic shows English, and says so. */
    ui.carLanguage = -1;
    ui.applyLanguage();
    CHECK(currentLanguage() == LangEn &&
          !strcmp(view(ui).items[3], "Language: Automatic (English)"));
    /* Anything but OK or an error is the worker's protocol error. */
    PRESS(ui, KeySelect);
    PRESS(ui, KeySelect);
    CHECK(answer(ui, "SET manager language auto", "WHAT") == -1);
    CHECK(ui.screen == Error && ui.fatal);
    setCurrentLanguage(LangEn);
}

/* Pixels in the title's rows that are not the background. */
static size_t titleInk(const nbt::gui::Frame &frame) {
    size_t lit = 0;
    for (size_t y = 10; y < 40; ++y) {
        for (size_t x = 16; x < 300; ++x) {
            uint32_t pixel;
            memcpy(&pixel, frame.pixels + y * frame.stride + x * 4, 4);
            lit += (pixel & 0xffffffu) != 0x0a0f17u;
        }
    }
    return lit;
}

static void test_rendering(void) {
    using namespace nbt::gui;
    static unsigned char pixels[640 * 240 * 4];
    Frame frame = {pixels, sizeof(pixels), 640, 240, 640 * 4};
    UiRenderer renderer;
    const struct {
        Language language;
        const char *title;
    } titles[] = {{LangJa, "設定"},          {LangZh, "设置"},         {LangFr, "Réglages État"},
                  {LangDe, "Zurück Öffnen"}, {LangZh, "中文 English"}, {LangEn, "日本語"}};
    for (size_t i = 0; i < sizeof(titles) / sizeof(titles[0]); ++i) {
        UiView v = UiView();
        v.screen = Apps;
        v.progress = -1;
        v.modal.progress = -1;
        snprintf(v.title, sizeof(v.title), "%s", titles[i].title);
        setCurrentLanguage(titles[i].language);
        CHECK(renderer.render(v, NULL, frame).ok());
        CHECK(titleInk(frame) > 100); // drawn, not blank or replaced
    }
    /* A character no font has (Greek) is skipped, not fatal. */
    UiView v = UiView();
    v.screen = Apps;
    v.progress = -1;
    v.modal.progress = -1;
    snprintf(v.title, sizeof(v.title), "%s", "Ωμέγα");
    CHECK(renderer.render(v, NULL, frame).ok());
    setCurrentLanguage(LangEn);
}

int main(void) {
    test_model();
    test_texts();
    test_worker_texts();
    test_car_record();
    test_language_screen();
    test_rendering();
    puts("language-test: OK");
    return 0;
}

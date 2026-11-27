// SPDX-License-Identifier: MPL-2.0
/* The manager screen's themes: the setting, the car's make from its CarPlay
 * configuration, the palettes and what the renderer draws with them. */
#include "nbt/gui/frame.hpp"
#include "nbt/gui/theme.hpp"
#include "nbt/gui/ui_renderer.hpp"
#include "nbt/model/theme.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace nbt::model;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static int makeOf(const char *text) {
    Theme theme = ThemeBmw;
    return themeFromCarConfig(text, strlen(text), theme) ? static_cast<int>(theme) : -1;
}

static void put(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    CHECK(f && fputs(text, f) >= 0 && !fclose(f));
}

static void test_model(void) {
    int choice = 7;
    CHECK(!strcmp(themeCode(ThemeBmw), "bmw") && !strcmp(themeCode(ThemeSupra), "supra") &&
          !themeCode(3) && !themeCode(kThemeAuto) && !strcmp(themeName(ThemeMini), "MINI"));
    CHECK(themeSettingParse("auto", choice) && choice == kThemeAuto);
    CHECK(themeSettingParse("mini", choice) && choice == ThemeMini);
    CHECK(!themeSettingParse("MINI", choice) && !themeSettingParse("", choice) &&
          !themeSettingParse(NULL, choice) && choice == ThemeMini);
    CHECK(!strcmp(themeSettingCode(kThemeAuto), "auto") && !strcmp(themeSettingCode(9), "auto") &&
          !strcmp(themeSettingCode(ThemeSupra), "supra"));
    /* A choice wins; Automatic is the car's make, BMW when it is unknown. */
    CHECK(themeEffective(ThemeMini, ThemeSupra) == ThemeMini);
    CHECK(themeEffective(kThemeAuto, ThemeSupra) == ThemeSupra);
    CHECK(themeEffective(kThemeAuto, -1) == ThemeBmw && themeEffective(5, 9) == ThemeBmw);
}

static void test_car_config(void) {
    CHECK(makeOf("#Version: 1.10\n[General]\nModel=G31-NBTEvo-0716\nManufacturer=BMW\n"
                 "OEMIcon=/opt/conn/data/carplay/OEM_BMW.png\nOEMIconLabel=BMW\n") == ThemeBmw);
    CHECK(makeOf("[General]\r\nManufacturer=Toyota\r\n[Screen]\r\nWidth=1280\r\n") == ThemeSupra);
    /* A MINI keeps BMW as its manufacturer: the logo comes first. */
    CHECK(makeOf("[General]\nManufacturer=BMW\nOEMIconLabel=MINI\n") == ThemeMini);
    CHECK(makeOf("[General]\nManufacturer=BMW\nOEMIcon=/opt/conn/data/carplay/OEM_MINI.png\n") ==
          ThemeMini);
    CHECK(makeOf("[General]\nOEMIcon=/x/OEM_TOYOTA.png\nOEMIconLabel=\n") == ThemeSupra);
    CHECK(makeOf("[General]\nOEMIcon=/opt/conn/data/carplay/OEM_BMWi.png\n") == ThemeBmw);
    CHECK(makeOf("[General]\nManufacturer=MINI") == ThemeMini); // no newline at the end
    /* Another make, another section, a comment, nothing: none of ours. */
    CHECK(makeOf("[General]\nManufacturer=Rolls-Royce\nOEMIconLabel=RR\n") == -1);
    CHECK(makeOf("[KnobInput]\nManufacturer=Toyota\n") == -1);
    CHECK(makeOf("Manufacturer=Toyota\n") == -1 &&
          makeOf("[General]\n#Manufacturer=Toyota\n") == -1);
    CHECK(makeOf("[General]\nManufacturerX=Toyota\nModel=MINI\n") == -1 && makeOf("") == -1);
    Theme theme = ThemeBmw;
    CHECK(!themeFromCarConfig(NULL, 4, theme) && !themeFromCarConfig("[General]\nMan", 13, theme));
}

static void test_files(void) {
    char dir[] = "/tmp/nbt-theme-XXXXXX", path[96], root[40], error[160];
    nbt_fs fs;
    int choice = 7;
    CHECK(mkdtemp(dir));
    snprintf(path, sizeof(path), "%s/carplay.ini", dir);
    CHECK(themeCarRead(path) == -1 && themeCarRead(NULL) == -1); // absent
    put(path, "[General]\nManufacturer=Toyota\n");
    CHECK(themeCarRead(path) == ThemeSupra);
    put(path, "[General]\nManufacturer=Rolls-Royce\n");
    CHECK(themeCarRead(path) == -1);
    /* manager/theme: one line; absent or anything else is Automatic. */
    snprintf(root, sizeof(root), "%s/managed", dir);
    CHECK(!mkdir(root, 0700) && !nbt_fs_init(&fs, root, error, sizeof(error)));
    CHECK(!themeSettingLoad(&fs, choice) && choice == kThemeAuto);
    snprintf(path, sizeof(path), "%s/manager", root);
    CHECK(!mkdir(path, 0700) || !access(path, F_OK));
    snprintf(path, sizeof(path), "%s/manager/theme", root);
    put(path, "supra\n");
    CHECK(!themeSettingLoad(&fs, choice) && choice == ThemeSupra);
    const char *const bad[] = {"supra", "supra\n\n", "red\n", "\n", "minimini\n"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        put(path, bad[i]);
        choice = 7;
        CHECK(!themeSettingLoad(&fs, choice) && choice == kThemeAuto);
    }
    /* The watch: the setting, else the car's make; a change shows within a second. */
    using namespace nbt::gui;
    char car[40];
    snprintf(car, sizeof(car), "%s/carplay.ini", dir);
    put(car, "[General]\nManufacturer=BMW\nOEMIconLabel=MINI\n");
    put(path, "auto\n");
    ThemeWatch watch;
    watch.start(root, car);
    CHECK(currentTheme() == ThemeMini);
    put(path, "supra\n");
    watch.poll(1);
    CHECK(currentTheme() == ThemeSupra);
    put(path, "bmw\n");
    watch.poll(500); // not yet due
    CHECK(currentTheme() == ThemeSupra);
    watch.poll(1001);
    CHECK(currentTheme() == ThemeBmw);
    ThemeWatch none;
    none.start(NULL, NULL);
    CHECK(currentTheme() == ThemeBmw);
}

static uint32_t pixel(const nbt::gui::Frame &frame, size_t x, size_t y) {
    uint32_t value;
    memcpy(&value, frame.pixels + y * frame.stride + x * 4, 4);
    return value & 0xffffffu;
}

static void test_rendering(void) {
    using namespace nbt::gui;
    static unsigned char pixels[640 * 240 * 4];
    Frame frame = {pixels, sizeof(pixels), 640, 240, 640 * 4};
    UiRenderer renderer;
    CHECK(&palette(7) == &palette(ThemeBmw) && &palette(-1) == &palette(ThemeBmw));
    UiView v = UiView(), other;
    v.screen = Apps;
    v.progress = 40;
    v.modal.progress = -1;
    snprintf(v.title, sizeof(v.title), "Menu");
    snprintf(v.items[0], sizeof(v.items[0]), "first");
    snprintf(v.items[1], sizeof(v.items[1]), "second");
    v.itemCount = 2;
    other = v;
    other.theme = ThemeMini;
    CHECK(v != other); // a theme change is drawn
    for (int theme = 0; theme < static_cast<int>(kThemeCount); ++theme) {
        const Palette &c = palette(theme);
        size_t accent = 0, cursor = 0;
        v.theme = theme;
        CHECK(renderer.render(v, NULL, frame).ok());
        CHECK(pixel(frame, 630, 238) == c.deep || c.look == LookBars); // below the list
        for (size_t y = 0; y < frame.height; ++y) {
            for (size_t x = 0; x < frame.width; ++x) {
                const uint32_t p = pixel(frame, x, y);
                accent += p == c.accent;
                cursor += x > 100 && x < 600 && p != c.background && p != c.track &&
                          p != c.accent && (p >> 16) < 0xa0; // the fill, not the white text
            }
        }
        CHECK(accent > 100); // the progress bar, and a theme's edge, rail or dot
        CHECK(cursor > 2000);
    }
}

int main(void) {
    umask(077); // the managed root takes private files only
    test_model();
    test_car_config();
    test_files();
    test_rendering();
    puts("theme-test: OK");
    return 0;
}

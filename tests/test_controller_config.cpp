// SPDX-License-Identifier: MPL-2.0
#include "nbt/ccs.h"
#include "nbt/controller_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static int parse(const char *text, nbt_ccs_config *c) {
    return nbt_controller_config_parse(text, strlen(text), c);
}

static int is_default(const nbt_ccs_config *c) {
    nbt_ccs_config d;
    nbt_ccs_defaults(&d);
    return !memcmp(c, &d, sizeof(d));
}

static size_t feed(nbt_ccs *ccs, unsigned inst, unsigned fkt, const unsigned char *data, size_t n,
                   nbt_input *out) {
    return nbt_ccs_feed(ccs, 0xe5, inst, fkt, 0xc, data, n, out, 8);
}

static void test_parse(void) {
    static const char *bad[] = {
        "",
        "NBTCTL1",
        "NBTCTL2\n",
        "nbtctl1\n",
        "NBTCTL1\nknob_instance=03",
        " NBTCTL1\n",
        "NBTCTL1\nknob_instance=3\n",
        "NBTCTL1\nknob_instance=003\n",
        "NBTCTL1\nknob_instance=0x3\n",
        "NBTCTL1\nknob_instance=g3\n",
        "NBTCTL1\nknob_instance = 03\n",
        "NBTCTL1\nknob_instance=03 \n",
        "NBTCTL1\nknob_instance=03\r\n",
        "NBTCTL1\nknob_instance=03\nknob_instance=03\n",
        "NBTCTL1\nknob=03\n",
        "NBTCTL1\nknob_instance\n",
        "NBTCTL1\n=03\n",
        "NBTCTL1\n  \n",
        "NBTCTL1\ninvert=2\n",
        "NBTCTL1\ninvert=\n",
        "NBTCTL1\nknob_menu_slot=7\n",
        "NBTCTL1\nknob_back_slot=0\n",
        "NBTCTL1\nccs_menu_slot=1\n",
        "NBTCTL1\nccs_back_slot=10\n",
        "NBTCTL1\ninstances=\n",
        "NBTCTL1\ninstances=01,\n",
        "NBTCTL1\ninstances=,01\n",
        "NBTCTL1\ninstances=01,,03\n",
        "NBTCTL1\ninstances=01, 03\n",
        "NBTCTL1\ninstances=01,03,01\n",
        "NBTCTL1\ninstances=01,02,03,04,05,06,07,08,09\n",
        "NBTCTL1\ninstances=01,02\n",  /* the knob instance 03 is not registered */
        "NBTCTL1\nknob_instance=07\n", /* not among the default instances */
        "NBTCTL1\n# tab\there\n",
        "NBTCTL1\nrelease=2\n",
        "NBTCTL1\nrelease=\n",
        "NBTCTL1\nrelease=01\n",
        "NBTCTL1\nrelease=1\nrelease=1\n"};
    nbt_ccs_config c;
    size_t i;
    CHECK(parse("NBTCTL1\n", &c) == 0 && is_default(&c));
    CHECK(parse("NBTCTL1\n\n# defaults\n\n", &c) == 0 && is_default(&c));
    CHECK(
        parse(
            "NBTCTL1\nknob_instance=03\nccs_instance=01\ninstances=01,02,03,05,10,12,14\ninvert=0\n"
            "knob_menu_slot=0\nknob_back_slot=1\nccs_menu_slot=0\nccs_back_slot=1\n",
            &c) == 0 &&
        is_default(&c));
    /* Any order, upper-case hexadecimal, omitted keys keep their default. */
    CHECK(parse("NBTCTL1\n# a J29 with a CCS35up knob at 0x0A\nknob_back_slot=4\ninstances=0A,01\n"
                "knob_instance=0A\ninvert=1\nknob_menu_slot=6\nccs_back_slot=3\n",
                &c) == 0);
    CHECK(c.knob_instance == 0x0a && c.ccs_instance == 0x01 && c.instance_count == 2 &&
          c.instances[0] == 0x0a && c.instances[1] == 0x01 && c.invert == 1 &&
          c.knob_menu_slot == 6 && c.knob_back_slot == 4 && c.ccs_menu_slot == 0 &&
          c.ccs_back_slot == 3);
    CHECK(parse("NBTCTL1\ninstances=01,02,03,04,05,06,07,08\n", &c) == 0 && c.instance_count == 8);
    CHECK(parse("NBTCTL1\nrelease=1\n", &c) == 0 && c.release == 1 && c.instance_count == 7);
    CHECK(parse("NBTCTL1\nrelease=0\n", &c) == 0 && is_default(&c));
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memset(&c, 0x5a, sizeof(c));
        if (parse(bad[i], &c) != -1 || !is_default(&c)) {
            fprintf(stderr, "accepted: %s\n", bad[i]);
            exit(1);
        }
    }
    {
        char text[NBT_CONTROLLER_CONFIG_MAX + 2];
        memset(text, '\n', sizeof(text));
        memcpy(text, "NBTCTL1\n", 8);
        CHECK(nbt_controller_config_parse(text, NBT_CONTROLLER_CONFIG_MAX, &c) == 0);
        CHECK(nbt_controller_config_parse(text, NBT_CONTROLLER_CONFIG_MAX + 1, &c) == -1 &&
              is_default(&c));
        memcpy(text + 8, "invert=1\n", 9);
        text[9] = 0; /* a NUL byte inside a key */
        CHECK(nbt_controller_config_parse(text, 40, &c) == -1);
    }
    {
        /* The shipped example spells out the defaults. */
        char text[NBT_CONTROLLER_CONFIG_MAX + 1];
        FILE *file = fopen("config/controller.conf.example", "rb");
        size_t n;
        CHECK(file);
        n = fread(text, 1, sizeof(text), file);
        CHECK(fclose(file) == 0 && n > 0 && n <= NBT_CONTROLLER_CONFIG_MAX);
        memset(&c, 0, sizeof(c));
        CHECK(nbt_controller_config_parse(text, n, &c) == 0 && is_default(&c));
    }
    CHECK(nbt_controller_config_parse(NULL, 4, &c) == -1 && is_default(&c));
    CHECK(nbt_controller_config_parse("NBTCTL1\n", 8, NULL) == -1);
    CHECK(nbt_controller_config_load(NULL, &c) == -1 && is_default(&c));
}

static void test_decoder_uses_config(void) {
    static const unsigned char menu_slot2[7] = {0, 0, 1, 0, 0, 0, 0},
                               slot0[7] = {1, 0, 0, 0, 0, 0, 0}, slot1[7] = {0, 1, 0, 0, 0, 0, 0},
                               released[7] = {0};
    nbt_ccs_config c;
    nbt_ccs ccs;
    nbt_input out[8];
    unsigned char rotary[8] = {1, 0, 10, 0, 0, 0, 0, 0xff};
    CHECK(parse("NBTCTL1\nknob_instance=05\nknob_menu_slot=2\nknob_back_slot=0\ninvert=1\n"
                "ccs_menu_slot=3\nccs_back_slot=4\n",
                &c) == 0);
    nbt_ccs_init(&ccs, &c);
    /* The default instance 0x03 is no longer the knob. */
    CHECK(feed(&ccs, 3, 0xc47, slot0, 7, out) == 0);
    CHECK(feed(&ccs, 5, 0xc47, slot0, 7, out) == 1 && out[0] == NBT_INPUT_BACK);
    CHECK(feed(&ccs, 5, 0xc47, released, 7, out) == 0);
    /* OPTION is the first slot that is neither MENU nor BACK: slot 1 here. */
    CHECK(feed(&ccs, 5, 0xc47, slot1, 7, out) == 1 && out[0] == NBT_INPUT_OPTION);
    CHECK(feed(&ccs, 5, 0xc47, released, 7, out) == 0);
    CHECK(feed(&ccs, 5, 0xc47, menu_slot2, 7, out) == 1 && out[0] == NBT_INPUT_HOME);
    /* Inverted rotation: a positive step moves up. */
    CHECK(feed(&ccs, 5, 0xc46, rotary, 8, out) == 0);
    rotary[2] = 12;
    CHECK(feed(&ccs, 5, 0xc46, rotary, 8, out) == 2 && out[0] == NBT_INPUT_UP &&
          out[1] == NBT_INPUT_UP);
    /* Older CCS hard keys use their own slots (sensor bits). */
    {
        unsigned char key[7] = {1, 0x1d, 0x22, 1, 0, 0, 1u << 4};
        CHECK(feed(&ccs, 1, 0xc20, key, 7, out) == 1 && out[0] == NBT_INPUT_BACK);
        key[3] = 0;
        CHECK(feed(&ccs, 1, 0xc20, key, 7, out) == 0);
        key[3] = 1;
        key[6] = 1u << 3;
        CHECK(feed(&ccs, 1, 0xc20, key, 7, out) == 1 && out[0] == NBT_INPUT_HOME);
        key[3] = 0;
        CHECK(feed(&ccs, 1, 0xc20, key, 7, out) == 0);
        key[3] = 1;
        key[6] = 1u << 1;
        CHECK(feed(&ccs, 1, 0xc20, key, 7, out) == 0);
        key[3] = 0;
        CHECK(feed(&ccs, 1, 0xc20, key, 7, out) == 0);
        key[3] = 1; /* OPTION: slot 0 here, MENU and BACK being 3 and 4 */
        key[6] = 1u << 0;
        CHECK(feed(&ccs, 1, 0xc20, key, 7, out) == 1 && out[0] == NBT_INPUT_OPTION);
    }
    /* An invalid configuration falls back to the defaults. */
    c.release = 2;
    CHECK(!nbt_ccs_config_valid(&c));
    c.release = 1;
    CHECK(nbt_ccs_config_valid(&c));
    c.knob_back_slot = c.knob_menu_slot;
    CHECK(!nbt_ccs_config_valid(&c) && !nbt_ccs_config_valid(NULL));
    nbt_ccs_init(&ccs, &c);
    CHECK(is_default(&ccs.config));
}

/* What the manager's Settings write parses back to the same configuration. */
static void test_format(void) {
    nbt_ccs_config c, back;
    char text[NBT_CONTROLLER_CONFIG_MAX + 1];
    int n;
    nbt_ccs_defaults(&c);
    n = nbt_controller_config_format(&c, text, sizeof(text));
    CHECK(n > 0 && (size_t)n == strlen(text) && !strncmp(text, "NBTCTL1\n", 8));
    CHECK(nbt_controller_config_parse(text, (size_t)n, &back) == 0 && is_default(&back));
    CHECK(parse("NBTCTL1\nknob_instance=1a\nccs_instance=02\ninstances=1a,02\ninvert=1\nknob_menu_"
                "slot=6\n"
                "knob_back_slot=5\nccs_menu_slot=2\nccs_back_slot=3\nrelease=1\n",
                &c) == 0);
    n = nbt_controller_config_format(&c, text, sizeof(text));
    CHECK(n > 0 && strstr(text, "\ninstances=1a,02\n") && strstr(text, "\nknob_instance=1a\n"));
    memset(&back, 0, sizeof(back));
    CHECK(nbt_controller_config_parse(text, (size_t)n, &back) == 0 &&
          !memcmp(&back, &c, sizeof(c)));
    /* All eight instances fit. */
    CHECK(parse("NBTCTL1\ninstances=01,02,03,04,05,06,07,ff\n", &c) == 0);
    n = nbt_controller_config_format(&c, text, sizeof(text));
    CHECK(n > 0 && strstr(text, "\ninstances=01,02,03,04,05,06,07,ff\n"));
    /* Invalid configurations and short buffers are refused. */
    CHECK(nbt_controller_config_format(&c, text, (size_t)n) == -1);
    CHECK(nbt_controller_config_format(&c, text, (size_t)n + 1) == n);
    CHECK(nbt_controller_config_format(&c, NULL, sizeof(text)) == -1);
    CHECK(nbt_controller_config_format(NULL, text, sizeof(text)) == -1);
    c.knob_back_slot = c.knob_menu_slot;
    CHECK(nbt_controller_config_format(&c, text, sizeof(text)) == -1);
}

int main(void) {
    test_parse();
    test_format();
    test_decoder_uses_config();
    return 0;
}

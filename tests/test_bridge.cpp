// SPDX-License-Identifier: MPL-2.0
#include "nbt/bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void status_lines(void) {
    const char *bad[] = {"",           "1 x",      "1 \n",
                         "01 x\n",     "-1 x\n",   "18446744073709551616 x\n",
                         "1 x\nextra", "1 x\r\n",  "1 a\tb\n",
                         "1 \x7f\n",   "1 \x80\n", "1 abcdefghijklmnopqrstuvwxy\n",
                         " 1 x\n"};
    char line[NBT_BRIDGE_STATUS_LINE_MAX + 1], text[NBT_BRIDGE_STATUS_MAX + 1];
    uint64_t sequence = 0;
    unsigned i;
    int n = nbt_bridge_status_format(UINT64_MAX, "abcdefghijklmnopqrstuvwx", line, sizeof(line));
    assert(n == NBT_BRIDGE_STATUS_LINE_MAX);
    assert(!nbt_bridge_status_parse(line, (size_t)n, &sequence, text));
    assert(sequence == UINT64_MAX && !strcmp(text, "abcdefghijklmnopqrstuvwx"));
    assert(nbt_bridge_status_format(1, "x", line, 4) < 0);
    assert(nbt_bridge_status_format(1, "", line, sizeof(line)) < 0);
    assert(!nbt_bridge_status_parse("0 ~| ()\n", 8, &sequence, text) && !sequence &&
           !strcmp(text, "~| ()"));
    assert(nbt_bridge_status_parse("1 x\0\n", 5, &sequence, text));
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(nbt_bridge_status_parse(bad[i], strlen(bad[i]), &sequence, text));
    }
    /* Every truncation of a maximum-sized valid line must be rejected. */
    n = nbt_bridge_status_format(UINT64_MAX, "abcdefghijklmnopqrstuvwx", line, sizeof(line));
    for (i = 0; i < (unsigned)n; ++i) {
        assert(nbt_bridge_status_parse(line, i, &sequence, text));
    }
}

/* bridge-most: present exactly when `most` is declared, valid, round-trips,
 * and needs a manager serving `most` even when optional. */
static void most_declarations(void) {
    nbt_bridge_provider provider, old;
    nbt_bridge_requirement need;
    nbt_bridge_version grant;
    char text[NBT_BRIDGE_TEXT_MAX], error[128], *p;
    const char *bad[] = {
        "bridge=1.1\nbridge-uses=most\nbridge-optional=\n",                       /* no policy */
        "bridge=1.1\nbridge-uses=\nbridge-optional=\nbridge-most=rx:05.00.621\n", /* not declared */
        "bridge=1.1\nbridge-uses=most\nbridge-optional=\nbridge-most=\n",         /* empty */
        "bridge=1.1\nbridge-uses=most\nbridge-optional=\nbridge-most=rx:05.00.62\n",
        "bridge=1.1\nbridge-uses=most\nbridge-optional=\nbridge-most=tx:*.*.*\n",
        "bridge=1.1\nbridge-uses=most\nbridge-optional=\nbridge-most=rx:05.00.621,rx:05.00.621\n",
        "bridge=1.1\nbridge-uses=most\nbridge-optional=\nbridge-most=rx:*.*.*,sub:05.00.621\n",
    };
    unsigned i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        strcpy(text, bad[i]);
        p = text;
        assert(nbt_bridge_requirement_parse(&p, "", &need) || *p);
    }
    strcpy(text, "new_bridge=1.1\nnew_bridge-uses=show\nnew_bridge-optional=most\n"
                 "new_bridge-most=rx:E5.*.*,tx:82.00.*,ctl:E5.03\nnext=1\n");
    p = text;
    assert(!nbt_bridge_requirement_parse(&p, "new_", &need) && !strcmp(p, "next=1\n"));
    assert(!strcmp(need.most, "rx:E5.*.*,tx:82.00.*,ctl:E5.03"));
    assert(nbt_bridge_requirement_format(&need, "new_", text, sizeof(text)) > 0);
    assert(strstr(text, "new_bridge-most=rx:E5.*.*,tx:82.00.*,ctl:E5.03\n"));
    nbt_bridge_current(&provider);
    assert(!nbt_bridge_check(&need, &provider, NULL, &grant, error, sizeof(error)) &&
           !strcmp(grant.caps, "show,most"));
    /* Unavailable at run time: an optional `most` is simply not granted. */
    assert(!nbt_bridge_check(&need, &provider, "show,status,live-settings", &grant, error,
                             sizeof(error)) &&
           !strcmp(grant.caps, "show"));
    memset(&old, 0, sizeof(old));
    assert(!nbt_bridge_provider_add(&old, "1.1|show,status,live-settings"));
    assert(nbt_bridge_check(&need, &old, NULL, NULL, error, sizeof(error)) &&
           strstr(error, "MOST"));
    /* A maximum-sized policy formats within NBT_BRIDGE_TEXT_MAX with long capability lists. */
    memset(&need, 0, sizeof(need));
    need.major = 1;
    for (i = 0; i < 9; ++i) {
        if (i) {
            strcat(need.uses, ",");
        }
        sprintf(need.uses + strlen(need.uses), "u%02u-%s", i, "abcdefghijklmnopqrst");
    }
    strcat(need.uses, ",most");
    for (i = 0; i < 9; ++i) {
        if (i) {
            strcat(need.optional, ",");
        }
        sprintf(need.optional + strlen(need.optional), "o%02u-%s", i, "abcdefghijklmnopqrst");
    }
    for (i = 0; i < 48; ++i) {
        if (i) {
            strcat(need.most, ",");
        }
        sprintf(need.most + strlen(need.most), "rx:%02X.00.%03X", i, i);
    }
    assert(strlen(need.most) <= NBT_BRIDGE_MOST_MAX);
    assert(nbt_bridge_requirement_format(&need, "old_", text, sizeof(text)) > 0);
    p = text;
    assert(!nbt_bridge_requirement_parse(&p, "old_", &need) && !*p);
}

int main(void) {
    status_lines();
    nbt_bridge_provider provider;
    nbt_bridge_requirement need;
    nbt_bridge_version grant;
    uint16_t major, minor;
    char text[NBT_BRIDGE_TEXT_MAX], error[128], *p;
    const char *bad[] = {"",      "0.0",  "01.0",    "1.00",    "1",
                         "1.2.3", "1.-1", "65536.0", "1.65536", "1.0\n"};
    unsigned i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(nbt_bridge_version_parse(bad[i], &major, &minor));
    }
    assert(!nbt_bridge_version_parse("65535.65535", &major, &minor));
    assert(major == 65535 && minor == 65535);
    assert(nbt_bridge_caps_valid(""));
    assert(nbt_bridge_caps_valid("show,status,live-settings"));
    assert(!nbt_bridge_caps_valid("show,show"));
    assert(!nbt_bridge_caps_valid("show,"));
    assert(!nbt_bridge_caps_valid(",show"));
    assert(!nbt_bridge_caps_valid("Show"));
    assert(!nbt_bridge_caps_valid("foo_bar"));
    memset(text, 'a', sizeof(text));
    text[256] = 0;
    assert(!nbt_bridge_caps_valid(text));
    nbt_bridge_current(&provider);
    assert(provider.count == 1 && provider.versions[0].major == 1 &&
           provider.versions[0].minor == 4 &&
           !strcmp(provider.versions[0].caps, "show,status,live-settings,most,gamepad,controller"));
    memset(&need, 0, sizeof(need));
    assert(!nbt_bridge_check(&need, &provider, "", &grant, error, sizeof(error)) && !grant.major);
    need.major = 1;
    strcpy(need.optional, "show,future-capability");
    assert(!nbt_bridge_check(&need, &provider, "", &grant, error, sizeof(error)));
    assert(grant.major == 1 && !grant.caps[0]);
    assert(!nbt_bridge_check(&need, &provider, "show", &grant, error, sizeof(error)));
    assert(!strcmp(grant.caps, "show"));
    strcpy(need.uses, "future");
    assert(nbt_bridge_check(&need, &provider, NULL, NULL, error, sizeof(error)));
    assert(strstr(error, "future"));
    strcpy(need.uses, "status");
    memset(&provider, 0, sizeof(provider));
    assert(!nbt_bridge_provider_add(&provider, "1.3|status,show"));
    assert(!nbt_bridge_provider_add(&provider, "2.0|status"));
    assert(nbt_bridge_provider_add(&provider, "3.0|"));
    need.minor = 2;
    assert(!nbt_bridge_check(&need, &provider, "status,show", &grant, error, sizeof(error)));
    assert(grant.major == 1 && grant.minor == 3 && !strcmp(grant.caps, "status,show"));
    assert(nbt_bridge_check(&need, &provider, "show", &grant, error, sizeof(error)));
    assert(strstr(error, "not available on this car"));
    need.minor = 4;
    assert(nbt_bridge_check(&need, &provider, NULL, NULL, error, sizeof(error)));
    need.minor = 2;
    assert(nbt_bridge_requirement_format(&need, "old_", text, sizeof(text)) > 0);
    p = text;
    memset(&need, 0, sizeof(need));
    assert(!nbt_bridge_requirement_parse(&p, "old_", &need) && !*p);
    assert(need.major == 1 && need.minor == 2 && !strcmp(need.uses, "status"));
    assert(nbt_bridge_requirement_format(&need, "", text, 1) < 0);
    strcpy(text, "bridge=1.0\nbridge-uses=show\nbridge-optional=show\n");
    p = text;
    assert(nbt_bridge_requirement_parse(&p, "", &need));
    memset(&provider, 0, sizeof(provider));
    assert(!nbt_bridge_provider_add(&provider, "2.0|"));
    assert(nbt_bridge_provider_add(&provider, "1.0|"));
    assert(nbt_bridge_provider_add(&provider, "2.1|"));
    most_declarations();
    puts("bridge checks passed");
    return 0;
}

// SPDX-License-Identifier: MPL-2.0
#include "nbt/bridge.h"
#include "nbt/cds_policy.h"
#include "nbt/companion_wire.h"
#include "nbt/diag/hsfz_tester.hpp"
#include "nbt/diag_policy.h"
#include "nbt/diag_wire.h"
#include "nbt/files_policy.h"
#include "nbt/net_policy.h"
#include "nbt/permissions.h"
#include "nbt/process/companion_files.hpp"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    need.minor = 5; // this manager runs 1.5 and later
    assert(!nbt_bridge_check(&need, &provider, NULL, &grant, error, sizeof(error)) &&
           !strcmp(grant.caps, "show,most"));
    /* Unavailable at run time: an optional `most` is simply not granted. */
    assert(!nbt_bridge_check(&need, &provider, "show,status,live-settings", &grant, error,
                             sizeof(error)) &&
           !strcmp(grant.caps, "show"));
    need.minor = 1;
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

/* A files policy: canonical extensions and limits; names a car can store. */
static void files_policies(void) {
    nbt_files_policy policy;
    const char *bad[] = {"",
                         "|16|1024",
                         "sfc|16",
                         "sfc|16|1024|1",
                         "sfc||1024",
                         "sfc|16|",
                         "sfc|0|1024",
                         "sfc|016|1024",
                         "sfc|16|8",
                         "sfc|4097|65536",
                         "sfc|16|65537",
                         "SFC|16|1024",
                         "s.c|16|1024",
                         "sfc,|16|1024",
                         ",sfc|16|1024",
                         "sfc,sfc|16|1024",
                         "abcdefghi|16|1024",
                         "sfc|+16|1024",
                         "sfc|16|1024\n",
                         "sfc|16|1024|",
                         "sfc|16|1024|folders",
                         "sfc|16|1024|folders=",
                         "sfc|16|1024|folders=smc",
                         "sfc|16|1024|folders=sfc,sfc",
                         "sfc|16|1024|folders=sfc|x",
                         "sfc|16|folders=sfc",
                         "a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q|1|1"};
    unsigned i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(nbt_files_policy_parse(bad[i], NULL));
    }
    assert(!nbt_files_policy_parse("a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p|4096|65536", &policy));
    assert(policy.count == 16 && policy.file_mib == 4096 && policy.quota_mib == 65536);
    assert(nbt_files_policy_quota_bytes(&policy) == UINT64_C(65536) << 20);
    assert(!nbt_files_policy_parse("cue,bin,chd|700|8000|folders=cue", &policy));
    assert(policy.folder_marks == 1 && policy.count == 3 && policy.quota_mib == 8000);
    assert(nbt_files_policy_marks_folder(&policy, "Game.CUE") &&
           !nbt_files_policy_marks_folder(&policy, "Game.bin") &&
           !nbt_files_policy_marks_folder(&policy, "Game.chd"));
    assert(!nbt_files_policy_parse("sfc,smc,fig,swc|16|1024", &policy));
    assert(policy.count == 4 && !strcmp(policy.extensions[3], "swc") && !policy.folder_marks);
    assert(nbt_files_listed_name_valid("Tekken 3/") && nbt_files_listed_name_valid("a.sfc"));
    assert(!nbt_files_listed_name_valid("a/b/") && !nbt_files_listed_name_valid("/") &&
           !nbt_files_listed_name_valid(".hidden/") && !nbt_files_listed_name_valid("x"));
    assert(nbt_files_folder_valid("Tekken 3") && !nbt_files_folder_valid("a/b") &&
           !nbt_files_folder_valid("end.") && nbt_files_folder_valid("Pok\xc3\xa9mon") &&
           !nbt_files_folder_valid("Pok\xe9mon"));
    assert(nbt_files_listed_name_valid("Pok\xc3\xa9mon/") &&
           nbt_files_char_length("\xc3\xa9", 2) == 2 && nbt_files_char_length("\xc3\xa9", 1) == 0 &&
           nbt_files_char_length("/", 1) == 0);
    assert(nbt_files_policy_file_bytes(&policy) == UINT64_C(16) << 20);
    const char *names[] = {"Super Game (E).sfc",
                           "a.SMC",
                           "x.y.fig",
                           "1.swc",
                           "caf\xc3\xa9.sfc",
                           "\xe3\x83\x9d\xe3\x82\xb1\xe3\x83\xa2\xe3\x83\xb3.sfc",
                           "\xf0\x9f\x8e\xae.sfc"};
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        assert(nbt_files_policy_accepts(&policy, names[i]));
    }
    const char *refused[] = {"game.zip", ".sfc", "game.", "game", " game.sfc", "game.sfc ",
                             "a/b.sfc", "a\\b.sfc", "a:b.sfc", "a*b.sfc", "a?b.sfc", "a\"b.sfc",
                             "a<b.sfc", "a|b.sfc", "a\tb.sfc", "game.sfc.zip",
                             // UTF-8 that is not a character a name may hold: a bare continuation
                             // byte, a cut sequence, an overlong form, a surrogate, a C1 control,
                             // beyond U+10FFFF, Latin-1.
                             "a\x80.sfc", "a\xc3.sfc", "a\xc0\xaf.sfc", "a\xed\xa0\x80.sfc",
                             "a\xc2\x85.sfc", "a\xf4\x90\x80\x80.sfc", "caf\xe9.sfc"};
    for (i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        assert(!nbt_files_policy_accepts(&policy, refused[i]));
    }
    char longest[NBT_FILES_NAME_MAX + 2];
    memset(longest, 'a', sizeof(longest));
    memcpy(longest + NBT_FILES_NAME_MAX - 4, ".sfc", 5);
    assert(nbt_files_policy_accepts(&policy, longest));
    memcpy(longest + NBT_FILES_NAME_MAX - 3, ".sfc", 5);
    assert(!nbt_files_name_valid(longest));
}

/* bridge-files: present exactly when `files` is declared, bridge 1.5 or
 * later, valid, round-trips, and needs a manager serving `files`. */
static void files_declarations(void) {
    nbt_bridge_provider provider, old;
    nbt_bridge_requirement need;
    nbt_bridge_version grant;
    char text[NBT_BRIDGE_TEXT_MAX], error[128], *p;
    const char *bad[] = {
        "bridge=1.5\nbridge-uses=files\nbridge-optional=\n",                      /* no policy */
        "bridge=1.5\nbridge-uses=\nbridge-optional=\nbridge-files=sfc|16|1024\n", /* undeclared */
        "bridge=1.4\nbridge-uses=files\nbridge-optional=\nbridge-files=sfc|16|1024\n", /* too old */
        "bridge=1.5\nbridge-uses=files\nbridge-optional=\nbridge-files=\n",
        "bridge=1.5\nbridge-uses=files\nbridge-optional=\nbridge-files=sfc|16|8\n",
        /* the files line after the MOST line, never before */
        "bridge=1.5\nbridge-uses=files,most\nbridge-optional=\nbridge-files=sfc|1|1\n"
        "bridge-most=rx:05.00.621\n",
    };
    unsigned i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        strcpy(text, bad[i]);
        p = text;
        assert(nbt_bridge_requirement_parse(&p, "", &need) || *p);
    }
    strcpy(text, "new_bridge=1.5\nnew_bridge-uses=show,files\nnew_bridge-optional=most\n"
                 "new_bridge-most=rx:E5.*.*\nnew_bridge-files=sfc,smc|16|1024\nnext=1\n");
    p = text;
    assert(!nbt_bridge_requirement_parse(&p, "new_", &need) && !strcmp(p, "next=1\n"));
    assert(!strcmp(need.files, "sfc,smc|16|1024") && !strcmp(need.most, "rx:E5.*.*"));
    assert(nbt_bridge_requirement_format(&need, "new_", text, sizeof(text)) > 0);
    assert(strstr(text, "new_bridge-most=rx:E5.*.*\nnew_bridge-files=sfc,smc|16|1024\n"));
    nbt_bridge_current(&provider);
    assert(!nbt_bridge_check(&need, &provider, NULL, &grant, error, sizeof(error)) &&
           !strcmp(grant.caps, "show,files,most"));
    /* No usable disk: a required `files` refuses the start, naming it. */
    assert(nbt_bridge_check(&need, &provider, "show,most", &grant, error, sizeof(error)) &&
           strstr(error, "needs files; not available on this car"));
    memset(&old, 0, sizeof(old));
    assert(!nbt_bridge_provider_add(&old, "1.5|show,most"));
    assert(nbt_bridge_check(&need, &old, NULL, NULL, error, sizeof(error)) &&
           strstr(error, "files"));
    /* Maximum fields format within NBT_BRIDGE_TEXT_MAX with the journal prefix. */
    memset(&need, 0, sizeof(need));
    need.major = 1;
    need.minor = 5;
    for (i = 0; i < 9; ++i) {
        sprintf(need.uses + strlen(need.uses), "%su%02u-%s", i ? "," : "", i,
                "abcdefghijklmnopqrst");
        sprintf(need.optional + strlen(need.optional), "%so%02u-%s", i ? "," : "", i,
                "abcdefghijklmnopqrst");
    }
    strcat(need.uses, ",most");
    strcat(need.optional, ",files");
    for (i = 0; i < 48; ++i) {
        sprintf(need.most + strlen(need.most), "%srx:%02X.00.%03X", i ? "," : "", i, i);
    }
    for (i = 0; i < 16; ++i) {
        sprintf(need.files + strlen(need.files), "%sext%05u", i ? "," : "", i);
    }
    strcat(need.files, "|4096|65536");
    assert(strlen(need.files) <= NBT_BRIDGE_FILES_MAX && !nbt_files_policy_parse(need.files, NULL));
    assert(nbt_bridge_requirement_format(&need, "old_", text, sizeof(text)) > 0);
    p = text;
    assert(!nbt_bridge_requirement_parse(&p, "old_", &need) && !*p);
}

/* Car-data and network policies (bridge 1.6): canonical names and rules. */
static void cds_net_policies(void) {
    nbt_cds_policy cds;
    nbt_net_policy net;
    const char *badCds[] = {"",     "gear", ".a.b",  "a.b.",  "a..b", "a.b,a.b",
                            "a.b,", ",a.b", "a-b.c", "a.b c", "ab"};
    unsigned i;
    for (i = 0; i < sizeof(badCds) / sizeof(badCds[0]); ++i) {
        assert(nbt_cds_policy_parse(badCds[i], NULL));
    }
    assert(!nbt_cds_policy_parse("driving.gear,sensors.PDCStatus,communication.callStatus0", &cds));
    assert(cds.count == 3 && !strcmp(cds.names[1], "sensors.PDCStatus"));
    char text[NBT_CDS_POLICY_MAX + 2];
    text[0] = 0;
    for (i = 0; i < 17; ++i) {
        sprintf(text + strlen(text), "%sa.%u", i ? "," : "", i);
    }
    assert(nbt_cds_policy_parse(text, NULL)); /* 17 names */
    char longName[NBT_CDS_NAME_MAX + 2];
    memset(longName, 'a', sizeof(longName));
    longName[1] = '.';
    longName[NBT_CDS_NAME_MAX] = 0;
    assert(!nbt_cds_policy_parse(longName, NULL));
    longName[NBT_CDS_NAME_MAX] = 'a';
    longName[NBT_CDS_NAME_MAX + 1] = 0;
    assert(nbt_cds_policy_parse(longName, NULL));
    const char *badNet[] = {"",
                            "tcp:0@uap0",
                            "tcp:05277@uap0",
                            "tcp:65536@uap0",
                            "udp:53@uap0",
                            "tcp:5277@eth0",
                            "tcp:5277@uap0+uap0",
                            "tcp:5277@",
                            "tcp:5277",
                            "tcp:5277@uap0,tcp:5277@wfd0",
                            "tcp:1@uap0,tcp:2@uap0,tcp:3@uap0,tcp:4@uap0,tcp:5@uap0",
                            "tcp:5277@uap0+",
                            "tcp:5277@uap0 "};
    for (i = 0; i < sizeof(badNet) / sizeof(badNet[0]); ++i) {
        assert(nbt_net_policy_parse(badNet[i], NULL));
    }
    assert(!nbt_net_policy_parse("tcp:5277@uap0+wfd0,tcp:65535@wfd0", &net));
    assert(net.count == 2 && net.rules[0].port == 5277 &&
           net.rules[0].interfaces == (NBT_NET_UAP0 | NBT_NET_WFD0) && net.rules[1].port == 65535 &&
           net.rules[1].interfaces == NBT_NET_WFD0);
    assert(!strcmp(nbt_net_interface_name(NBT_NET_WFD0), "wfd0") && !nbt_net_interface_name(3));
}

/* bridge-cds and bridge-net: present exactly when `car-data` and `net` are
 * declared, bridge 1.6 or later, after the files line, round-trip, and need a
 * manager serving them; the largest declaration formats within
 * NBT_BRIDGE_TEXT_MAX. */
static void cds_net_declarations(void) {
    nbt_bridge_provider provider, old;
    nbt_bridge_requirement need;
    nbt_bridge_version grant;
    char text[NBT_BRIDGE_TEXT_MAX], error[128], *p;
    const char *bad[] = {
        "bridge=1.6\nbridge-uses=\nbridge-optional=car-data\n",                  /* no policy */
        "bridge=1.6\nbridge-uses=\nbridge-optional=\nbridge-cds=driving.gear\n", /* undeclared */
        "bridge=1.5\nbridge-uses=\nbridge-optional=car-data\nbridge-cds=driving.gear\n", /* old */
        "bridge=1.6\nbridge-uses=\nbridge-optional=net\nbridge-net=tcp:0@uap0\n",
        "bridge=1.5\nbridge-uses=\nbridge-optional=net\nbridge-net=tcp:5277@uap0\n",
        "bridge=1.6\nbridge-uses=\nbridge-optional=\nbridge-net=tcp:5277@uap0\n",
        /* the network line after the car-data line, never before */
        "bridge=1.6\nbridge-uses=\nbridge-optional=car-data,net\nbridge-net=tcp:5277@uap0\n"
        "bridge-cds=driving.gear\n",
    };
    unsigned i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        strcpy(text, bad[i]);
        p = text;
        assert(nbt_bridge_requirement_parse(&p, "", &need) || *p);
    }
    strcpy(text,
           "new_bridge=1.6\nnew_bridge-uses=status\nnew_bridge-optional=hold,car-data,net\n"
           "new_bridge-cds=driving.gear,sensors.PDCStatus\nnew_bridge-net=tcp:5277@uap0+wfd0\n"
           "next=1\n");
    p = text;
    assert(!nbt_bridge_requirement_parse(&p, "new_", &need) && !strcmp(p, "next=1\n"));
    assert(!strcmp(need.cds, "driving.gear,sensors.PDCStatus") &&
           !strcmp(need.net, "tcp:5277@uap0+wfd0"));
    assert(nbt_bridge_requirement_format(&need, "new_", text, sizeof(text)) > 0);
    assert(strstr(text, "new_bridge-cds=driving.gear,sensors.PDCStatus\n"
                        "new_bridge-net=tcp:5277@uap0+wfd0\n"));
    nbt_bridge_current(&provider);
    assert(provider.versions[0].minor == 9);
    assert(!nbt_bridge_check(&need, &provider, NULL, &grant, error, sizeof(error)) &&
           !strcmp(grant.caps, "status,hold,car-data,net"));
    memset(&old, 0, sizeof(old));
    assert(!nbt_bridge_provider_add(&old, "1.6|status,hold"));
    assert(nbt_bridge_check(&need, &old, NULL, NULL, error, sizeof(error)) &&
           strstr(error, "car-data"));
    /* The largest declaration, journal prefix included. */
    memset(&need, 0, sizeof(need));
    need.major = 1;
    need.minor = 6;
    for (i = 0; i < 9; ++i) {
        sprintf(need.uses + strlen(need.uses), "%su%02u-%s", i ? "," : "", i,
                "abcdefghijklmnopqrst");
        sprintf(need.optional + strlen(need.optional), "%so%02u-%s", i ? "," : "", i,
                "abcdefghijklmnopqrst");
    }
    strcat(need.uses, ",most");
    strcat(need.optional, ",files,car-data,net");
    for (i = 0; i < 48; ++i) {
        sprintf(need.most + strlen(need.most), "%srx:%02X.00.%03X", i ? "," : "", i, i);
    }
    for (i = 0; i < 16; ++i) {
        sprintf(need.files + strlen(need.files), "%sext%05u", i ? "," : "", i);
    }
    strcat(need.files, "|4096|65536");
    for (i = 0; i < 16 && strlen(need.cds) + 16 <= NBT_BRIDGE_CDS_MAX; ++i) {
        sprintf(need.cds + strlen(need.cds), "%sabcdefghij.k%02u", i ? "," : "", i);
    }
    strcpy(need.net,
           "tcp:65531@uap0+wfd0,tcp:65532@uap0+wfd0,tcp:65533@uap0+wfd0,tcp:65534@wfd0+uap0");
    assert(strlen(need.net) <= NBT_BRIDGE_NET_MAX && !nbt_net_policy_parse(need.net, NULL));
    assert(!nbt_cds_policy_parse(need.cds, NULL));
    assert(nbt_bridge_requirement_format(&need, "old_", text, sizeof(text)) > 0);
    p = text;
    assert(!nbt_bridge_requirement_parse(&p, "old_", &need) && !*p);
}

/* Diagnostics (bridge 1.8): the policy, the request classes and the wire. */
static void diag_policy_and_wire(void) {
    nbt_diag_policy policy;
    const char *bad[] = {"",
                         "12",
                         "12:",
                         "1:read",
                         "12:write",
                         "12:clear+read",
                         "12:read+read",
                         "12:read,12:codes",
                         "40:read,12:read",
                         "12:read,",
                         "12:READ",
                         "1g:read",
                         "12:read+",
                         "12:read+drive",
                         "12:drive",
                         "12:control+clear"};
    unsigned i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(nbt_diag_policy_parse(bad[i], NULL));
    }
    assert(!nbt_diag_policy_parse("12:read+codes+clear,40:read,60:codes", &policy));
    assert(policy.count == 3 && policy.rules[0].ecu == 0x12 && policy.rules[0].classes == 7 &&
           policy.rules[1].ecu == 0x40 && policy.rules[1].classes == NBT_DIAG_READ &&
           policy.rules[2].classes == NBT_DIAG_CODES);
    const uint8_t read[] = {0x22, 0xf4, 0x0c}, readOdd[] = {0x22, 0xf4}, define[] = {0x2c, 0x01},
                  codes[] = {0x19, 0x02, 0xff}, clear[] = {0x14, 0xff, 0xff, 0xff},
                  session[] = {0x10, 0x03}, programming[] = {0x10, 0x02},
                  write[] = {0x2e, 0xf1, 0x90, 0x01}, routine[] = {0x31, 0x01, 0x0f, 0x0b},
                  security[] = {0x27, 0x01}, reset[] = {0x11, 0x01};
    assert(nbt_diag_request_class(read, sizeof(read)) == NBT_DIAG_READ);
    assert(!nbt_diag_request_class(readOdd, sizeof(readOdd)));
    assert(nbt_diag_request_class(define, sizeof(define)) == NBT_DIAG_READ);
    assert(nbt_diag_request_class(codes, sizeof(codes)) == NBT_DIAG_CODES);
    assert(nbt_diag_request_class(clear, sizeof(clear)) == NBT_DIAG_CLEAR);
    assert(nbt_diag_request_class(session, sizeof(session)) == NBT_DIAG_SESSION);
    assert(!nbt_diag_request_class(programming, sizeof(programming)));
    assert(nbt_diag_request_class(write, sizeof(write)) == NBT_DIAG_CONTROL);
    assert(nbt_diag_request_class(routine, sizeof(routine)) == NBT_DIAG_CONTROL);
    assert(!nbt_diag_request_class(security, sizeof(security)));
    assert(!nbt_diag_request_class(reset, sizeof(reset)));
    assert(nbt_diag_policy_allows(&policy, 0x12, clear, sizeof(clear)) == NBT_DIAG_CLEAR);
    assert(!nbt_diag_policy_allows(&policy, 0x40, clear, sizeof(clear)));
    assert(!nbt_diag_policy_allows(&policy, 0x60, read, sizeof(read)));
    assert(!nbt_diag_policy_allows(&policy, 0x13, read, sizeof(read)));
    // Control and drive (bridge 1.9): writes, routines and input/output control,
    // parked unless `drive`; a session with clear or control.
    const uint8_t io[] = {0x2f, 0x60, 0x50, 0x03, 0x01}, ioShort[] = {0x2f, 0x60, 0x50},
                  routineBad[] = {0x31, 0x04, 0x0f, 0x0b};
    assert(nbt_diag_request_class(io, sizeof(io)) == NBT_DIAG_CONTROL);
    assert(!nbt_diag_request_class(ioShort, sizeof(ioShort)));
    assert(!nbt_diag_request_class(routineBad, sizeof(routineBad)));
    assert(nbt_diag_policy_allows(&policy, 0x12, session, sizeof(session)) == NBT_DIAG_SESSION);
    assert(!nbt_diag_policy_allows(&policy, 0x40, session, sizeof(session)));
    assert(!nbt_diag_policy_allows(&policy, 0x12, io, sizeof(io)));
    assert(!nbt_diag_policy_parse("12:read+control,29:clear+drive,40:control+drive", &policy));
    assert(nbt_diag_policy_allows(&policy, 0x12, io, sizeof(io)) == NBT_DIAG_CONTROL);
    assert(nbt_diag_policy_allows(&policy, 0x12, session, sizeof(session)) == NBT_DIAG_SESSION);
    assert(nbt_diag_policy_allows(&policy, 0x29, clear, sizeof(clear)) ==
           (NBT_DIAG_CLEAR | NBT_DIAG_DRIVE));
    assert(!nbt_diag_policy_allows(&policy, 0x29, io, sizeof(io)));
    assert(nbt_diag_policy_allows(&policy, 0x40, write, sizeof(write)) ==
           (NBT_DIAG_CONTROL | NBT_DIAG_DRIVE));
    assert(!nbt_diag_policy_allows(&policy, 0x40, read, sizeof(read)));
    uint8_t wire[NBT_DIAG_WIRE_MAX];
    nbt_diag_request request;
    nbt_diag_reply reply;
    int n = nbt_diag_request_encode(0x12, 0x1234, 0, read, sizeof(read), wire, sizeof(wire));
    assert(n == 9 && !nbt_diag_request_decode(wire, (size_t)n, &request));
    assert(request.ecu == 0x12 && request.id == 0x1234 && request.timeout_ms == 2000 &&
           request.size == 3 && !memcmp(request.uds, read, 3));
    assert(nbt_diag_request_decode(wire, 6, &request)); /* no UDS bytes */
    wire[4] = 0x11, wire[5] = 0x27;                     /* 10001 ms */
    assert(nbt_diag_request_decode(wire, (size_t)n, &request));
    const uint8_t answer[] = {0x62, 0xf4, 0x0c, 0x1a};
    n = nbt_diag_reply_encode(0x12, 7, NBT_DIAG_OK, NBT_DIAG_VIA_HSFZ, answer, sizeof(answer), wire,
                              sizeof(wire));
    assert(n == 10 && !nbt_diag_reply_decode(wire, (size_t)n, &reply));
    assert(reply.status == NBT_DIAG_OK && reply.size == 4 && reply.uds[0] == 0x62);
    n = nbt_diag_reply_encode(0x12, 8, NBT_DIAG_REFUSED, NBT_DIAG_VIA_NONE, NULL, 0, wire,
                              sizeof(wire));
    assert(n == 6 && !nbt_diag_reply_decode(wire, 6, &reply) && reply.status == NBT_DIAG_REFUSED);
    assert(nbt_diag_reply_encode(0x12, 8, NBT_DIAG_REFUSED, 0, answer, 4, wire, sizeof(wire)) < 0);
    assert(nbt_diag_reply_decode(wire, 7, &reply)); /* bytes with a refusal */
    wire[4] = 9;
    assert(nbt_diag_reply_decode(wire, 6, &reply));
    nbt_bridge_requirement need;
    char text[NBT_BRIDGE_TEXT_MAX], *p;
    char record[] = "bridge=1.8\nbridge-uses=diag\nbridge-optional=\nbridge-diag=12:read\n";
    p = record;
    assert(!nbt_bridge_requirement_parse(&p, "", &need) && !strcmp(need.diag, "12:read"));
    assert(nbt_bridge_requirement_format(&need, "", text, sizeof(text)) > 0 &&
           strstr(text, "bridge-diag=12:read\n"));
    char old[] = "bridge=1.7\nbridge-uses=diag\nbridge-optional=\nbridge-diag=12:read\n";
    p = old;
    assert(nbt_bridge_requirement_parse(&p, "", &need)); /* diag came with 1.8 */
    char missing[] = "bridge=1.8\nbridge-uses=diag\nbridge-optional=\n";
    p = missing;
    assert(nbt_bridge_requirement_parse(&p, "", &need)); /* diag needs its policy */
    char control18[] = "bridge=1.8\nbridge-uses=diag\nbridge-optional=\nbridge-diag=12:control\n";
    p = control18;
    assert(nbt_bridge_requirement_parse(&p, "", &need)); /* control came with 1.9 */
    char control19[] = "bridge=1.9\nbridge-uses=diag\nbridge-optional=\nbridge-diag=12:control\n";
    p = control19;
    assert(!nbt_bridge_requirement_parse(&p, "", &need));
}

/* The gateway other diagnostic tools on the head unit are set to. */
static void gateway_file(void) {
    char path[] = "/tmp/nbt-gateway-XXXXXX", out[48];
    const int fd = mkstemp(path);
    assert(fd >= 0);
    const char *const valid[][2] = {{"169.254.10.3\n", "169.254.10.3"},
                                    {"  10.0.0.2:6811 trailing\r\n", "10.0.0.2:6811"}};
    for (unsigned i = 0; i < 2; ++i) {
        assert(!ftruncate(fd, 0) && pwrite(fd, valid[i][0], strlen(valid[i][0]), 0) > 0);
        assert(nbt::diag::gatewayFromFile(path, out, sizeof(out)) && !strcmp(out, valid[i][1]));
    }
    const char *const invalid[] = {"", "\n", "gateway\n", "300.1.1.1\n", "1.2.3.4:0\n"};
    for (const char *text : invalid) {
        assert(!ftruncate(fd, 0) && (!*text || pwrite(fd, text, strlen(text), 0) > 0));
        assert(!nbt::diag::gatewayFromFile(path, out, sizeof(out)));
    }
    close(fd);
    unlink(path);
    assert(!nbt::diag::gatewayFromFile(path, out, sizeof(out)));
}

/* Background services (bridge 1.9): `service` with `show`, granted with it. */
static void service_declarations(void) {
    nbt_bridge_requirement need;
    nbt_bridge_provider provider;
    nbt_bridge_version grant;
    char error[128], *p;
    char record[] = "bridge=1.9\nbridge-uses=show\nbridge-optional=service\n";
    p = record;
    assert(!nbt_bridge_requirement_parse(&p, "", &need));
    nbt_bridge_current(&provider);
    assert(!nbt_bridge_check(&need, &provider, NULL, &grant, error, sizeof(error)) &&
           !strcmp(grant.caps, "show,service"));
    char old[] = "bridge=1.8\nbridge-uses=show\nbridge-optional=service\n";
    p = old;
    assert(nbt_bridge_requirement_parse(&p, "", &need)); /* service came with 1.9 */
    char screenless[] = "bridge=1.9\nbridge-uses=status\nbridge-optional=service\n";
    p = screenless;
    assert(nbt_bridge_requirement_parse(&p, "", &need)); /* runs in the background already */
}

/* Phone companions (bridge 1.9, docs/companion-bridge.md): the declaration,
 * the page's frame, the event lines and the files the worker keeps. */
static void companion_declarations_and_files(void) {
    nbt_bridge_requirement need;
    char error[128], *p;
    char record[] = "bridge=1.9\nbridge-uses=status\nbridge-optional=companion\n";
    p = record;
    assert(!nbt_bridge_requirement_parse(&p, "", &need) &&
           nbt_permissions(&need) == NBT_PERM_COMPANION);
    char old[] = "bridge=1.8\nbridge-uses=status\nbridge-optional=companion\n";
    p = old;
    assert(nbt_bridge_requirement_parse(&p, "", &need)); /* came with 1.9 */
    (void)error;
    uint64_t sequence = 0;
    const char page[] = "12\ntitle\tBoost\nbutton\tgo\tGo\n";
    assert(!nbt_companion_page_check(page, strlen(page), &sequence) && sequence == 12);
    const char *const badPages[] = {"",          "\n",          "012\ntext\ta\n", "1\ntext\ta",
                                    "x\ntext\n", "1\n\ntext\n", "1\ntext\x01\n",  "1 \n"};
    for (const char *bad : badPages) {
        assert(nbt_companion_page_check(bad, strlen(bad), &sequence));
    }
    char big[NBT_COMPANION_PAGE_MAX + 2];
    memset(big, 'a', sizeof(big));
    memcpy(big, "1\n", 2);
    big[NBT_COMPANION_PAGE_MAX - 1] = '\n';
    assert(!nbt_companion_page_check(big, NBT_COMPANION_PAGE_MAX, &sequence));
    big[NBT_COMPANION_PAGE_MAX] = '\n';
    assert(nbt_companion_page_check(big, NBT_COMPANION_PAGE_MAX + 1, &sequence));
    char widgets[256] = "1\n";
    for (unsigned i = 0; i < NBT_COMPANION_WIDGETS_MAX + 1; ++i) {
        strcat(widgets, "t\n");
    }
    assert(nbt_companion_page_check(widgets, strlen(widgets), &sequence)); /* 33 */
    assert(!nbt_companion_page_check(widgets, strlen(widgets) - 2, &sequence));
    char line[NBT_COMPANION_EVENT_LINE_MAX + 1], id[NBT_COMPANION_ID_MAX + 1],
        value[NBT_COMPANION_VALUE_MAX + 1];
    assert(nbt_companion_event_format(3, "mode", "fast", line, sizeof(line)) == 12 &&
           !strcmp(line, "3\tmode\tfast\n"));
    assert(!nbt_companion_event_parse(line, 12, &sequence, id, value) && sequence == 3 &&
           !strcmp(id, "mode") && !strcmp(value, "fast"));
    assert(!nbt_companion_event_parse("4\tgo\t\n", 6, &sequence, id, value) && !value[0]);
    const char *const badEvents[] = {"4\tGo\t\n", "4\tgo\n",  "4\tgo\ta,b\n",  "04\tgo\t\n",
                                     "\tgo\t\n",  "4\t\t1\n", "4\tgo\t1\t2\n", "4\tgo\t1"};
    for (const char *bad : badEvents) {
        assert(nbt_companion_event_parse(bad, strlen(bad), &sequence, id, value));
    }
    assert(nbt_companion_event_format(1, "go", "a,b", line, sizeof(line)) == -1);
    assert(nbt_companion_event_format(1, "a-very-long-identifier-25", "", line, sizeof(line)) ==
           -1);
    /* The worker's files: events numbered on, the last 16 kept; pages checked. */
    char dir[] = "/tmp/nbt-companion-XXXXXX", path[256], out[NBT_COMPANION_PAGE_MAX + 1];
    assert(mkdtemp(dir));
    assert(nbt::process::companionPageRead(dir, "test", out, sizeof(out)) == 0);
    for (unsigned i = 0; i < 20; ++i) {
        assert(!nbt::process::companionEventAdd(dir, "test", "go", i % 2 ? "1" : "0"));
    }
    assert(nbt::process::companionEventAdd(dir, "test", "Go", "") == -1);
    assert(nbt::process::companionEventAdd(dir, "manager", "go", "") == -1);
    assert(!nbt::process::companionEventsPath(dir, "test", path, sizeof(path)));
    FILE *events = fopen(path, "r");
    assert(events);
    unsigned count = 0;
    while (fgets(line, sizeof(line), events)) {
        assert(!nbt_companion_event_parse(line, strlen(line), &sequence, id, value) &&
               sequence == 5 + count++);
    }
    fclose(events);
    assert(count == NBT_COMPANION_EVENTS_MAX);
    snprintf(path, sizeof(path), "%s/nbt-bridge1-test.page", dir);
    FILE *written = fopen(path, "w");
    assert(written && fputs(page, written) >= 0 && !fclose(written));
    assert(nbt::process::companionPageRead(dir, "test", out, sizeof(out)) ==
               static_cast<int>(strlen(page)) &&
           !memcmp(out, page, strlen(page)));
    written = fopen(path, "w");
    assert(written && fputs("x\n", written) >= 0 && !fclose(written));
    assert(nbt::process::companionPageRead(dir, "test", out, sizeof(out)) == 0);
    nbt::process::companionRemove(dir, "test");
    assert(access(path, F_OK) && !rmdir(dir));
}

int main(void) {
    status_lines();
    companion_declarations_and_files();
    diag_policy_and_wire();
    service_declarations();
    gateway_file();
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
           provider.versions[0].minor == 9 &&
           provider.versions[0].oldest == NBT_BRIDGE_OLDEST_MINOR &&
           !strcmp(provider.versions[0].caps, "show,status,live-settings,most,gamepad,controller,"
                                              "files,hold,car-data,net,runner,diag,service,"
                                              "companion"));
    /* Bridge 1.7: an application built without the QNX SDP requires runner. */
    memset(&need, 0, sizeof(need));
    need.major = 1;
    need.minor = 7;
    strcpy(need.uses, "runner,status");
    assert(!nbt_bridge_check(&need, &provider, NULL, &grant, error, sizeof(error)) &&
           !strcmp(grant.caps, "runner,status"));
    {
        nbt_bridge_provider older;
        memset(&older, 0, sizeof(older));
        assert(!nbt_bridge_provider_add(&older, "1.6|status,hold"));
        assert(nbt_bridge_check(&need, &older, NULL, NULL, error, sizeof(error)));
    }
    /* Older than the oldest version this manager runs: disabled until updated,
     * with the reason. No bridge at all still runs, with no grant. */
    memset(&need, 0, sizeof(need));
    assert(!nbt_bridge_check(&need, &provider, "", &grant, error, sizeof(error)) && !grant.major);
    need.major = 1;
    need.minor = 4;
    strcpy(need.optional, "show");
    assert(nbt_bridge_check(&need, &provider, NULL, NULL, error, sizeof(error)) &&
           !strcmp(error, "declares bridge 1.4; this manager runs 1.5 and later"));
    need.minor = 5;
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
    need.minor = 2; // release lines state no oldest version: every minor still runs
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
    files_policies();
    files_declarations();
    cds_net_declarations();
    cds_net_policies();
    puts("bridge checks passed");
    return 0;
}

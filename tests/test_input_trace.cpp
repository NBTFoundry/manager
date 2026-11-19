#include "nbt/most/input_trace.hpp"

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

namespace {

void testFormat() {
    nbt::most::InputTrace trace;
    const unsigned char tilt[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    trace.record(1000, 0x05, 0x00, 0xC46, 0xC, tilt, sizeof(tilt), 1);
    const unsigned char longer[12] = {0xAA, 0xBB};
    trace.record(1100, 0xE5, 0x03, 0x123, 0x2, longer, sizeof(longer), 0);
    char line[120];
    CHECK(trace.format(0, 1120, line, sizeof(line)));
    CHECK(!strcmp(line, "-120 ms 05.00 fkt C46 op C len 6: 00 01 02 03 04 05 -> 1"));
    CHECK(trace.format(1, 1120, line, sizeof(line)));
    CHECK(!strcmp(line, "-20 ms E5.03 fkt 123 op 2 len 12: AA BB 00 00 00 00 00 00 .. -> 0"));
    CHECK(!trace.format(2, 1120, line, sizeof(line)));
    char tiny[10];
    CHECK(trace.format(0, 1120, tiny, sizeof(tiny)) && strlen(tiny) == sizeof(tiny) - 1);
}

// The oldest messages give way; a dump forgets what it logged.
void testRingAndDump() {
    nbt::most::InputTrace trace;
    const unsigned char byte = 0;
    for (unsigned i = 0; i < nbt::most::kTraceEntries + 3; ++i) {
        trace.record(i, 0x05, 0, i, 0xC, &byte, 1, 0);
    }
    char line[120];
    CHECK(trace.format(0, 100, line, sizeof(line)));
    CHECK(strstr(line, "fkt 003 ") != NULL); // 0, 1 and 2 dropped
    CHECK(trace.format(nbt::most::kTraceEntries - 1, 100, line, sizeof(line)));
    CHECK(!trace.format(nbt::most::kTraceEntries, 100, line, sizeof(line)));
    CHECK(trace.dump("a test", 100) == nbt::most::kTraceEntries);
    CHECK(!trace.format(0, 100, line, sizeof(line)));
    CHECK(trace.dump("a test", 100) == 0);
    trace.record(5, 0x05, 0, 1, 0xC, NULL, 0, 2);
    CHECK(trace.format(0, 5, line, sizeof(line)));
    CHECK(!strcmp(line, "-0 ms 05.00 fkt 001 op C len 0: -> 2"));
}

} // namespace

int main() {
    testFormat();
    testRingAndDump();
    puts("input trace tests passed");
    return 0;
}

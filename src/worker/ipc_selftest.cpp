// SPDX-License-Identifier: MPL-2.0
/* SELFTEST PREPARE, REPORT and LAST: Settings › Self test (docs/self-test.md,
 * PROTOCOL.md). PREPARE runs on the worker's job and is answered when it
 * ends; REPORT sums up what the bundled applications found once the GUI ran
 * them. */
#include "nbt/log.h"
#include "nbt/worker/ipc_dispatcher.hpp"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace worker {
namespace {

bool answer(char c) {
    return c == 'y' || c == 'n' || c == '-';
}

} // namespace

core::Status IpcDispatcher::handleSelfTest(const char *cursor, uint64_t nowMs) {
    char reply[2048];
    if (cursor && !strcmp(cursor, "PREPARE")) {
        if (worker_.state().session.active(nowMs) || worker_.state().updateLock.valid() ||
            !worker_.startSelfTest()) {
            return control_.respond("ERR busy\n");
        }
        selfTestPending_ = true;
        return core::Status();
    }
    if (cursor && !strcmp(cursor, "LAST")) {
        return control_.respond(
            selfTestLast(root_.legacy(), reply, sizeof(reply)) ? "SELFTEST\tnone\n" : reply);
    }
    // REPORT <saw> <heard>: y, n or - each.
    if (!cursor || strncmp(cursor, "REPORT ", 7) || strlen(cursor) != 10 || !answer(cursor[7]) ||
        cursor[8] != ' ' || !answer(cursor[9])) {
        return control_.respond("ERR invalid-command\n");
    }
    if (!selfTestPrepared_) {
        return control_.respond("ERR selftest\n");
    }
    selfTestPrepared_ = false;
    if (selfTestReport(root_.legacy(), prepared_, cursor[7], cursor[9], reply, sizeof(reply)) < 0 &&
        strncmp(reply, "SELFTEST\t", 9)) {
        return control_.respond("ERR selftest\n");
    }
    return control_.respond(reply);
}

core::Status IpcDispatcher::finishSelfTest() {
    int result = 0;
    if (!worker_.takeSelfTest(&result, &prepared_)) {
        return core::Status();
    }
    selfTestPending_ = false;
    selfTestPrepared_ = true;
    char reply[256];
    if (result < 0) {
        nbt_logf("self test: %s", prepared_.reason);
    }
    // A release that could not be verified still gets a report: it says so.
    return control_.respond(selfTestReadyReply(prepared_, reply, sizeof(reply)) ? "ERR selftest\n"
                                                                                : reply);
}

} // namespace worker
} // namespace nbt

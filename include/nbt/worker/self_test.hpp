// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_SELF_TEST_HPP
#define NBT_WORKER_SELF_TEST_HPP

/* Settings › Self test (docs/self-test.md): the worker's side. Prepare runs
 * on the worker's job: it verifies the running slot's release against the
 * manager key and installs, or keeps, each bundled application from it
 * (nbt/bundled.h). The report, on the loop once the GUI has run them, adds
 * the worker's own checks and the driver's answers to what each application
 * found, keeps it in state/manager/selftest.txt and answers the GUI. */

#include "nbt/fs.h"

#include <stddef.h>

namespace nbt {
namespace worker {

struct SelfTestPrepared {
    unsigned ready;   // bit i: nbt_bundled_apps[i] is installed and its last report gone
    unsigned absent;  // bit i: the release does not carry it
    unsigned failed;  // bit i: it could not be installed
    char version[49]; // the running release's version; "" when it could not be verified
    char reason[64];  // why not, for the log and the report
};

/* 0, or -1 when the release could not be verified (out.reason). */
int selfTestPrepare(nbt_fs *fs, SelfTestPrepared &out);
/* "SELFTEST\tready\t<app>,<app>,...\n" (no names: nothing to run). */
int selfTestReadyReply(const SelfTestPrepared &prepared, char *reply, size_t size);
/* Gathers the report (`saw`, `heard`: 'y', 'n' or '-'), keeps it and writes
 * the GUI's reply (docs/self-test.md, "Reply"). 0, or -1 when it could not be
 * kept (the reply is still written). */
int selfTestReport(nbt_fs *fs, const SelfTestPrepared &prepared, char saw, char heard, char *reply,
                   size_t size);
/* The reply for the kept report; -1 when there is none or it is unreadable. */
int selfTestLast(nbt_fs *fs, char *reply, size_t size);
/* GET /v1/selftest (PROTOCOL.md): the kept report as it is, or
 * "SELFTEST1\tnone\n" without one; an HTTP status. */
int selfTestPhone(nbt_fs *fs, char *out, size_t size);

} // namespace worker
} // namespace nbt

#endif

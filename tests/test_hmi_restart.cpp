/* The iDrive-only restart and the shim trial against a fake process manager. */
#include "nbt/launcher/hmi_restart.hpp"
#include "nbt/launcher/shim_trial.hpp"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

using nbt::launcher::HmiRestartOps;
using nbt::launcher::runHmiRestart;
using nbt::launcher::runShimTrial;
using nbt::launcher::ShimTrialOps;
using nbt::storage::ShimRecord;
using nbt::storage::ShimState;

namespace {

struct Fake {
    pid_t running;         // the HmiMain3D pid, 0 when none
    pid_t next;            // the pid RUN starts
    unsigned stopTakesMs;  // after STOP, when HmiMain3D ends (0: never)
    unsigned startTakesMs; // after RUN, when the new one appears (0: never)
    int stopError, runError;
    uint64_t now, stopAt, runAt;
    unsigned stops, runs;
    bool stopping, starting;
    // The environment file and the process manager reading it at each start.
    bool env, envIgnored, withShim, envWhileRunning;
    bool keepsEnv; // the loaded shim does not delete the file
    int envOnError, envOffError;
    unsigned dieMs; // with the shim, iDrive ends this long after starting (0: never)
    uint64_t startedAt;
    ShimState state; // the last state recorded
};

void advance(Fake &f) {
    if (f.stopping && f.stopTakesMs && f.now >= f.stopAt + f.stopTakesMs) {
        f.running = 0;
        f.stopping = false;
    }
    if (f.starting && f.startTakesMs && !f.running && f.now >= f.runAt + f.startTakesMs) {
        f.running = f.next++;
        f.starting = false;
        f.withShim = f.env && !f.envIgnored;
        f.env = f.env && (!f.withShim || f.keepsEnv); // the shim deletes it when loaded
        f.startedAt = f.now;
    }
    if (f.running && f.withShim && f.dieMs && f.now >= f.startedAt + f.dieMs) {
        f.running = 0;
    }
}
pid_t findHmi(void *c) {
    Fake &f = *static_cast<Fake *>(c);
    advance(f);
    return f.running;
}
bool alive(void *c, pid_t pid) {
    Fake &f = *static_cast<Fake *>(c);
    advance(f);
    return f.running == pid;
}
int request(void *c, uint32_t command) {
    Fake &f = *static_cast<Fake *>(c);
    if (command == nbt::launcher::kStarterStop) {
        ++f.stops;
        f.stopping = !f.stopError;
        f.stopAt = f.now;
        return f.stopError;
    }
    assert(command == nbt::launcher::kStarterRun);
    ++f.runs;
    f.starting = !f.runError;
    f.runAt = f.now;
    return f.runError;
}
uint64_t nowMs(void *c) {
    return static_cast<Fake *>(c)->now;
}
void sleepMs(void *c, unsigned ms) {
    static_cast<Fake *>(c)->now += ms;
}

int envOn(void *c) {
    Fake &f = *static_cast<Fake *>(c);
    f.envWhileRunning = f.envWhileRunning || f.running == 100; // the iDrive being stopped
    f.env = !f.envOnError;
    return f.envOnError;
}
int envOff(void *c) {
    Fake &f = *static_cast<Fake *>(c);
    f.env = f.env && f.envOffError;
    return f.envOffError;
}
bool shimLoaded(void *c, pid_t pid) {
    Fake &f = *static_cast<Fake *>(c);
    advance(f);
    return f.running == pid && f.withShim;
}
int envPresent(void *c) {
    return static_cast<Fake *>(c)->env ? 1 : 0;
}
int setState(void *c, ShimState state) {
    static_cast<Fake *>(c)->state = state;
    return 0;
}

HmiRestartOps restartOps(Fake &f) {
    const HmiRestartOps ops = {&findHmi, &alive, &request, &nowMs, &sleepMs, &f, NULL, NULL};
    return ops;
}

int trial(Fake &f, char *text, size_t size) {
    ShimTrialOps ops;
    ops.restart = restartOps(f);
    ops.envOn = &envOn;
    ops.envOff = &envOff;
    ops.envPresent = &envPresent;
    ops.shimLoaded = &shimLoaded;
    ops.setState = &setState;
    FILE *out = fmemopen(text, size, "w");
    assert(out);
    const int rc = runShimTrial(ops, out);
    fclose(out);
    return rc;
}

int run(Fake &f, char *text, size_t size) {
    const HmiRestartOps ops = restartOps(f);
    FILE *out = fmemopen(text, size, "w");
    assert(out);
    const int rc = runHmiRestart(ops, out);
    fclose(out);
    return rc;
}

Fake fake() {
    Fake f;
    memset(&f, 0, sizeof(f));
    f.running = 100;
    f.next = 200;
    f.stopTakesMs = 800;
    f.startTakesMs = 1500;
    f.now = 5000;
    return f;
}

} // namespace

int main() {
    char text[1024];
    Fake f = fake();
    assert(run(f, text, sizeof(text)) == 0);
    assert(strstr(text, "HMI_RESTART stopped after 800 ms") &&
           strstr(text, "HMI_RESTART OK HmiMain3D pid 200"));
    assert(f.stops == 1 && f.runs == 1 && f.running == 200);

    f = fake(); // not running: nothing requested
    f.running = 0;
    assert(run(f, text, sizeof(text)) == 1 && strstr(text, "is not running") && !f.stops &&
           !f.runs);

    f = fake(); // STOP refused: nothing changed, RUN never sent
    f.stopError = EACCES;
    assert(run(f, text, sizeof(text)) == 1 && strstr(text, "STOP request: errno 13") && !f.runs);

    f = fake(); // never stops: RUN is still requested, then failure
    f.stopTakesMs = 0;
    assert(run(f, text, sizeof(text)) == 1);
    assert(strstr(text, "still running after 10000 ms; requesting RUN anyway") && f.runs == 1);

    f = fake(); // stopped but never comes back within 30 s
    f.startTakesMs = 0;
    assert(run(f, text, sizeof(text)) == 1 && strstr(text, "did not start within 30 s"));

    f = fake(); // RUN refused after a stop
    f.runError = EIO;
    assert(run(f, text, sizeof(text)) == 1 && strstr(text, "RUN request: errno 5"));

    char log[2048];
    f = fake(); // passes: written after the stop, deleted by the shim, then armed again
    assert(trial(f, log, sizeof(log)) == 0);
    assert(strstr(log, "loaded the shim; watching it for 20 s") && strstr(log, "SHIM_TRIAL OK"));
    assert(f.env && !f.envWhileRunning && f.withShim && f.state == nbt::storage::ShimKept);
    assert(f.running == 200);

    f = fake(); // the process manager did not read the file: it stays armed, iDrive as is
    f.envIgnored = true;
    assert(trial(f, log, sizeof(log)) == 1 && strstr(log, "started without the shim"));
    assert(f.env && !f.state && f.running == 200 && f.runs == 1);

    f = fake(); // iDrive ends with the shim: failed, started again without it
    f.dieMs = 3000;
    assert(trial(f, log, sizeof(log)) == 1);
    assert(strstr(log, "ended after 3000 ms") && strstr(log, "back without the shim, pid 201"));
    assert(!f.env && !f.withShim && f.state == nbt::storage::ShimFailed && f.running == 201);

    f = fake(); // the shim leaves the file: a failed start would repeat, so off
    f.keepsEnv = true;
    assert(trial(f, log, sizeof(log)) == 1 && strstr(log, "did not delete"));
    assert(!f.env && f.state == nbt::storage::ShimFailed && f.running == 200);

    f = fake(); // the file cannot be written: iDrive still comes back, without the shim
    f.envOnError = ENOSPC;
    assert(trial(f, log, sizeof(log)) == 1 && strstr(log, "preparing the start failed: errno 28"));
    assert(f.running == 200 && !f.withShim && !f.state);

    f = fake(); // iDrive not running: nothing requested, no file
    f.running = 0;
    assert(trial(f, log, sizeof(log)) == 1 && !f.stops && !f.runs && !f.env);

    f = fake(); // iDrive never comes back after the trial's RUN: failed, run again without it
    f.startTakesMs = 0;
    assert(trial(f, log, sizeof(log)) == 1 && strstr(log, "did not come back") && !f.env);
    assert(f.state == nbt::storage::ShimFailed);

    // The automatic check, 60 s after iDrive started (loaded, file still there).
    using nbt::launcher::decideShimCheck;
    using nbt::launcher::ShimDecision;
    ShimRecord r;
    memset(&r, 0, sizeof(r));
    r.state = nbt::storage::ShimArmed;
    ShimDecision d = decideShimCheck(r, true, false); // ran 60 s with it: kept, armed again
    assert(d.state == nbt::storage::ShimKept && d.strikes == 0 && d.env == 1);
    d = decideShimCheck(r, false, true); // not read yet (no restart since the update)
    assert(d.state == nbt::storage::ShimArmed && d.env == 0);
    d = decideShimCheck(r, false, false); // a start with it ended early: once more
    assert(d.state == nbt::storage::ShimArmed && d.strikes == 1 && d.env == 1);
    r.strikes = 1;
    r.state = nbt::storage::ShimKept;
    d = decideShimCheck(r, false, false); // twice: off
    assert(d.state == nbt::storage::ShimFailed && d.strikes == 2 && d.env == 0);
    d = decideShimCheck(r, true, false); // a good start clears the strikes
    assert(d.state == nbt::storage::ShimKept && d.strikes == 0 && d.env == 1);
    d = decideShimCheck(r, true, true); // loaded but the file stayed: off, file removed
    assert(d.state == nbt::storage::ShimFailed && d.env == -1);
    r.state = nbt::storage::ShimFailed;
    d = decideShimCheck(r, true, false); // failed or off: left alone
    assert(d.state == nbt::storage::ShimFailed && d.env == 0);
    r.state = nbt::storage::ShimOff;
    d = decideShimCheck(r, false, false);
    assert(d.state == nbt::storage::ShimOff && d.env == 0);
    memset(&r, 0, sizeof(r)); // no record: armed
    d = decideShimCheck(r, false, false);
    assert(d.state == nbt::storage::ShimArmed && d.env == 1);
    puts("hmi-restart tests: OK");
    return 0;
}

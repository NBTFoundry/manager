// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/worker.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);          \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

using nbt::core::Result;
using nbt::core::Span;
using nbt::core::Status;
using nbt::core::UniqueFd;
using nbt::process::ChildProcess;
using nbt::process::ChildRunner;
using nbt::process::HostControl;
using nbt::storage::ManagedRoot;
using nbt::worker::Ended;
using nbt::worker::Operation;
using nbt::worker::StartResult;
using nbt::worker::State;
using nbt::worker::Worker;

static int fail_poll;
static int fail_reap;
static int terminations;
static unsigned reap_calls;

class FakeChildren : public ChildRunner {
public:
    Result<ChildProcess> start(const ManagedRoot &root, const char *app, char *const *environment,
                               int outputFd, uint64_t nowMs) override {
        (void)root;
        (void)app;
        (void)environment;
        (void)outputFd;
        return Result<ChildProcess>::success(ChildProcess::forTesting(42, nowMs));
    }

    Result<bool> poll(ChildProcess &child, uint64_t nowMs) override {
        (void)child;
        (void)nowMs;
        if (fail_poll) {
            return Result<bool>::failure(Status::error(EIO, "injected poll failure"));
        }
        return Result<bool>::success(true);
    }

    bool healthy(const ChildProcess &child, uint64_t nowMs) override {
        return nowMs >= child.legacy().ready_ms && nowMs - child.legacy().ready_ms >= 30000;
    }

    Status stop(ChildProcess &child) override {
        child.resetForTesting();
        return Status();
    }

    Status terminate(ChildProcess &child, uint64_t nowMs) override {
        if (child.running() && !child.stopping()) {
            child.setStoppingForTesting(nowMs);
            ++terminations;
            reap_calls = 0;
        }
        return Status();
    }

    Result<bool> reap(ChildProcess &child, uint64_t nowMs) override {
        (void)nowMs;
        if (!child.running()) {
            return Result<bool>::success(false);
        }
        if (fail_reap || !child.stopping()) {
            return Result<bool>::failure(Status::error(EIO, "injected reap failure"));
        }
        if (reap_calls++ == 0) {
            return Result<bool>::success(true);
        }
        child.resetForTesting();
        return Result<bool>::success(false);
    }
};

static int host_running;
static int host_dying;
static int host_stuck;
static int host_starts;
static int host_terms;
static int host_kills;
static int host_kill_ignored;

class FakeHosts : public HostControl {
public:
    Status start(const ManagedRoot &root, const char *app, bool automatic) override {
        (void)root;
        (void)app;
        (void)automatic;
        host_running = 1;
        host_dying = 0;
        ++host_starts;
        return Status();
    }

    Result<pid_t> pid(const ManagedRoot &root, const char *app) override {
        (void)root;
        (void)app;
        if (!host_running) {
            return Result<pid_t>::success(0);
        }
        if (host_dying && !host_stuck && host_dying++ >= 2) {
            host_running = 0;
            return Result<pid_t>::success(0);
        }
        return Result<pid_t>::success(4242);
    }

    Status signal(const ManagedRoot &root, const char *app, int number) override {
        (void)root;
        (void)app;
        if (!host_running) {
            return Status();
        }
        if (number == SIGTERM) {
            ++host_terms;
            if (!host_dying) {
                host_dying = 1;
            }
        } else if (number == SIGKILL) {
            ++host_kills;
            if (!host_kill_ignored) {
                host_running = 0;
            }
        }
        return Status();
    }

    Result<bool> show(const ManagedRoot &root, const char *app) override {
        (void)root;
        (void)app;
        return Result<bool>::success(true);
    }

    Status status(const ManagedRoot &root, const char *app, Span<char> text) override {
        (void)root;
        (void)app;
        if (text.size()) {
            text.data()[0] = 0;
        }
        return Status();
    }
};

/* Waits for the worker's job (verification, install copy) and collects its
 * end at `now`, as the loop does, without advancing anything else. */
static void settle(Worker &worker, uint64_t now) {
    for (int i = 0; i < 10000 && worker.taskPending(); ++i) {
        if (!worker.collectTask(now)) {
            const struct timespec pause = {0, 1000000};
            nanosleep(&pause, NULL);
        }
    }
}

static int upload(Worker &worker, const char *path, bool opened) {
    unsigned char buffer[4096];
    if (!worker.openSession("test", 10).ok()) {
        return -1;
    }
    if (opened) {
        worker.pageOpened();
    }
    UniqueFd source(open(path, O_RDONLY));
    Result<UniqueFd> target = worker.uploadBegin(11);
    if (!source.valid() || !target.get()) {
        return -1;
    }
    ssize_t count;
    while ((count = read(source.get(), buffer, sizeof(buffer))) > 0) {
        if (write(target.value().get(), buffer, static_cast<size_t>(count)) != count) {
            return -1;
        }
    }
    if (count < 0 || fsync(target.value().get()) || !target.value().close().ok()) {
        return -1;
    }
    const Status endStatus = worker.uploadEnd(12);
    if (!endStatus.ok()) {
        CHECK(strlen(endStatus.message()) > 0);
        return -1;
    }
    settle(worker, 12);
    return worker.state().state == State::Verified ? 0 : -1;
}

static int running(Worker &worker, const char *app) {
    Result<bool> result = worker.running(app);
    if (!result.get()) {
        CHECK(strlen(result.status().message()) > 0);
        return -1;
    }
    return result.value() ? 1 : 0;
}

static int start(Worker &worker, const char *app, uint64_t nowMs) {
    Result<StartResult> result = worker.start(app, nowMs);
    if (!result.get()) {
        CHECK(strlen(result.status().message()) > 0);
        return -1;
    }
    return result.value() == StartResult::AlreadyRunning ? 1 : 0;
}

static int stop(Worker &worker, const char *app, uint64_t nowMs) {
    const Status st = worker.stop(app, nowMs);
    if (!st.ok()) {
        CHECK(strlen(st.message()) > 0);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    ManagedRoot root;
    FakeChildren children;
    FakeHosts hosts;
    unsigned char digest[32];
    if (argc != 4 || !root.initialize(argv[1]).ok()) {
        return 2;
    }
    const char *scenario = argv[3];
    const bool initiallyRunning = !strcmp(scenario, "running") ||
                                  !strcmp(scenario, "running-fail") || !strcmp(scenario, "stop") ||
                                  !strcmp(scenario, "stopping-approve") ||
                                  !strcmp(scenario, "host-stuck");
    const bool stopFirst = !strcmp(scenario, "stop") || !strcmp(scenario, "stopping-approve") ||
                           !strcmp(scenario, "host-stuck");
    Worker worker(root, children, hosts);
    nbt::worker::WorkerState &state = worker.state();
    CHECK(running(worker, "test") == 0 && running(worker, "manager") == -1);
    if (initiallyRunning) {
        CHECK(!start(worker, "test", 0) && host_running && host_starts == 1 &&
              state.child.pid() == 0);
        CHECK(!strcmp(state.message, "Application started") && running(worker, "test") == 1);
        CHECK(start(worker, "test", 0) == 1 && host_starts == 1 &&
              !strcmp(state.message, "Application already running"));
    }
    if (stopFirst) {
        host_stuck = !strcmp(scenario, "host-stuck");
        CHECK(!stop(worker, "test", 1) && state.op == Operation::HostStop && host_terms == 1);
        CHECK(!strcmp(state.message, "Stopping application"));
        CHECK(start(worker, "test", 2) == -1 && stop(worker, "test", 2) == -1);
    }
    if (!strcmp(scenario, "stop")) {
        worker.poll(3);
        CHECK(state.op == Operation::HostStop && host_running);
        worker.poll(4);
        CHECK(!host_running && state.op == Operation::None &&
              !strcmp(state.message, "Application stopped"));
        CHECK(!stop(worker, "test", 5) && state.op == Operation::None &&
              !strcmp(state.message, "Application stopped"));
        CHECK(!start(worker, "test", 5) && host_starts == 2);
        worker.shutdown();
        return 0;
    }
    if (!strcmp(scenario, "host-stuck")) {
        worker.poll(nbt::worker::kHostKillMs);
        CHECK(state.op == Operation::HostStop && !host_kills);
        worker.poll(1 + nbt::worker::kHostKillMs);
        CHECK(host_kills == 1 && !host_running);
        worker.poll(2 + nbt::worker::kHostKillMs);
        CHECK(state.op == Operation::None && !strcmp(state.message, "Application stopped"));
        CHECK(!start(worker, "test", 3 + nbt::worker::kHostKillMs) && host_running);
        host_kill_ignored = 1;
        CHECK(!stop(worker, "test", 10) && state.op == Operation::HostStop);
        worker.poll(10 + nbt::worker::kHostKillMs);
        CHECK(state.op == Operation::HostStop && host_kills == 2);
        worker.poll(10 + nbt::worker::kHostGiveUpMs);
        CHECK(state.op == Operation::None && state.state == State::Failed &&
              !strcmp(state.message, "cannot stop application"));
        worker.shutdown();
        return 0;
    }
    CHECK(!upload(worker, argv[2], strcmp(scenario, "watch-unopened") != 0));
    memcpy(digest, state.pending.package_hash, sizeof(digest));
    if (!strcmp(scenario, "wrong-digest")) {
        digest[0] ^= 1;
        CHECK(!worker.approve(digest, false, 13).ok() && state.state == State::Verified);
        worker.cancel(14);
        worker.shutdown();
        return 0;
    }
    if (!strcmp(scenario, "watch-cancel") || !strcmp(scenario, "watch-unopened")) {
        worker.cancel(14);
        CHECK(state.state == State::Idle && state.ended == Ended::Cancelled);
        worker.shutdown();
        return 0;
    }
    if (!strcmp(scenario, "watch-expire")) {
        worker.poll(10 + nbt::worker::Session::kTtlMs);
        CHECK(state.state == State::Idle && state.ended == Ended::Expired);
        worker.shutdown();
        return 0;
    }
    CHECK(worker.approve(state.pending.package_hash, false, 13).ok());
    CHECK(state.state == State::Installing && state.updateLock.valid());
    CHECK(!state.session.active(0));
    CHECK(start(worker, "test", 13) == -1 && stop(worker, "test", 13) == -1);
    if (initiallyRunning && !stopFirst) {
        CHECK(state.op == Operation::HostInstall && host_terms == 1 && state.previouslyRunning);
        CHECK(!strcmp(state.message, "Stopping application"));
        worker.poll(14);
        CHECK(state.op == Operation::HostInstall && host_running);
    }
    if (!strcmp(scenario, "stopping-approve")) {
        CHECK(!state.previouslyRunning);
    }
    for (uint64_t now = 15; state.op != Operation::None && now < 20; ++now) {
        worker.poll(now);
    }
    settle(worker, 15);
    CHECK(state.state == State::Installing && state.op == Operation::None &&
          state.child.pid() == 42 && !state.child.stopping());
    CHECK(!strcmp(state.message, "Checking application health"));
    if (!strcmp(scenario, "fail") || !strcmp(scenario, "running-fail")) {
        fail_poll = 1;
        worker.poll(21);
        CHECK(state.state == State::Installing && state.op == Operation::Restore);
        worker.poll(22);
        worker.poll(23);
        CHECK(state.state == State::Failed && state.child.pid() == 0 && !state.updateLock.valid());
        CHECK(!strcmp(state.message, "Update failed; previous application restored"));
        CHECK(host_running == initiallyRunning && host_starts == (initiallyRunning ? 2 : 0));
        worker.shutdown();
        return 0;
    }
    worker.poll(30015);
    CHECK(state.state == State::Installing && state.op == Operation::Confirmed &&
          !state.updateLock.valid());
    CHECK(!strcmp(state.message, initiallyRunning && !stopFirst ? "Restarting application"
                                                                : "Stopping application"));
    if (!strcmp(scenario, "reap-error")) {
        fail_reap = 1;
        worker.poll(30016);
        CHECK(state.state == State::Failed && state.op == Operation::None &&
              !strcmp(state.message, "installed, but cannot restore stopped state"));
        fail_reap = 0;
        worker.shutdown();
        return 0;
    }
    worker.poll(30016);
    worker.poll(30017);
    CHECK(state.state == State::Installed && state.child.pid() == 0);
    CHECK(host_running == (initiallyRunning && !stopFirst) &&
          host_starts == (initiallyRunning && !stopFirst ? 2 : initiallyRunning));
    worker.shutdown();
    return 0;
}

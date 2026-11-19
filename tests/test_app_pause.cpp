// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/app_pause.hpp"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

namespace {

// A child leading its own process group, as the host's applications do.
nbt_process startChild() {
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (!pid) {
        setpgid(0, 0);
        for (;;) {
            pause();
        }
    }
    (void)setpgid(pid, pid);
    nbt_process child;
    memset(&child, 0, sizeof(child));
    child.pid = pid;
    child.ready = 1;
    child.ready_fd = -1;
    child.lease_fd = -1;
    return child;
}

// The next state change of the child: 'T' stopped, 'C' continued, 'S' signalled.
char next(pid_t pid, int *signal) {
    int status = 0;
    CHECK(waitpid(pid, &status, WUNTRACED | WCONTINUED) == pid);
    if (WIFSTOPPED(status)) {
        return 'T';
    }
    if (WIFCONTINUED(status)) {
        return 'C';
    }
    CHECK(WIFSIGNALED(status));
    *signal = WTERMSIG(status);
    return 'S';
}

void testPauseResumeAndClose() {
    nbt_process child = startChild();
    int signal = 0;
    nbt::process::AppPause pause(100);
    CHECK(!pause.paused() && !pause.due(1000000));
    CHECK(pause.pause(child, NULL, 1000));
    CHECK(pause.paused());
    CHECK(next(child.pid, &signal) == 'T');
    // A stopped child sends no heartbeat: the host's check is held.
    child.last_heartbeat_ms = 0;
    pause.hold(child, 1050);
    CHECK(child.last_heartbeat_ms == 1050);
    CHECK(!pause.due(1099) && pause.due(1100));
    CHECK(pause.resume(child, NULL, 2000));
    CHECK(!pause.paused() && child.last_heartbeat_ms == 2000);
    CHECK(next(child.pid, &signal) == 'C');
    pause.hold(child, 2500); // running again: the clock is the child's
    CHECK(child.last_heartbeat_ms == 2000);
    // Paused again and closed: the stop must reach a stopped child.
    CHECK(pause.pause(child, NULL, 3000));
    CHECK(next(child.pid, &signal) == 'T');
    pause.beforeStop(child);
    CHECK(!pause.paused());
    char state;
    while ((state = next(child.pid, &signal)) == 'C') {
    }
    CHECK(state == 'S' && signal == SIGTERM);
}

void testNotPausedDoesNothing() {
    nbt_process child = startChild();
    int signal = 0;
    nbt::process::AppPause pause(100);
    CHECK(pause.resume(child, NULL, 10)); // nothing to continue
    child.last_heartbeat_ms = 5;
    pause.hold(child, 50);
    CHECK(child.last_heartbeat_ms == 5);
    pause.beforeStop(child); // not paused: no signal
    CHECK(kill(child.pid, SIGKILL) == 0);
    CHECK(next(child.pid, &signal) == 'S' && signal == SIGKILL);
}

} // namespace

int main() {
    testPauseResumeAndClose();
    testNotPausedDoesNothing();
    puts("app pause tests passed");
    return 0;
}

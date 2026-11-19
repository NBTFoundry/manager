#include "nbt/launcher/slot_state.hpp"
#include "nbt/manager_state.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static nbt_release release(unsigned char byte, uint64_t sequence) {
    nbt_release value;
    memset(&value, 0, sizeof(value));
    memset(value.sha256, byte, sizeof(value.sha256));
    value.sequence = sequence;
    return value;
}

int main(void) {
    nbt_manager_state state, parsed;
    nbt_release v1 = release(1, 1), v2 = release(2, 2), v3 = release(3, 3);
    char text[NBT_MANAGER_STATE_MAX], changed[NBT_MANAGER_STATE_MAX],
        path[] = "/tmp/nbt-state-XXXXXX";
    int length, lease, ready[2], stop[2], status;
    size_t i;
    pid_t child;
    nbt_process process;
    if (nbt_manager_state_initial(&state, &v1) || state.pending || state.active.slot ||
        state.highwater != 1) {
        return 1;
    }
    length = nbt_manager_state_format(&state, text, sizeof(text));
    if (length < 0 || nbt_manager_state_parse(text, (size_t)length, &parsed) ||
        parsed.active.sequence != 1 || parsed.previous.slot != 2 || parsed.restarts) {
        return 1;
    }
    memcpy(changed, text, (size_t)length + 1);
    changed[9] = '1';
    {
        char *fields = strstr(changed, "restarts=");
        if (!fields) {
            return 1;
        }
        *fields = 0;
        if (nbt_manager_state_parse(changed, (size_t)(fields - changed), &parsed) ||
            parsed.restarts || parsed.restart_epoch) {
            return 1;
        }
    }
    {
        nbt::core::Result<nbt::launcher::SlotState> value =
            nbt::launcher::SlotState::fromLegacy(parsed);
        unsigned delay = 0;
        if (!value.ok() ||
            value.value().recordRestart(1000, &delay) != nbt::launcher::RestartDecision::Restart ||
            delay != 500 ||
            value.value().recordRestart(1001, &delay) != nbt::launcher::RestartDecision::Restart ||
            delay != 1000 ||
            value.value().recordRestart(1002, &delay) != nbt::launcher::RestartDecision::Restart ||
            delay != 2000 ||
            value.value().recordRestart(1003, &delay) !=
                nbt::launcher::RestartDecision::Exhausted) {
            return 1;
        }
        nbt::core::Result<size_t> formatted = value.value().format(changed, sizeof(changed));
        if (!formatted.ok() || nbt_manager_state_parse(changed, formatted.value(), &parsed) ||
            parsed.restarts != 3) {
            return 1;
        }
        value = nbt::launcher::SlotState::fromLegacy(parsed);
        if (!value.ok() ||
            value.value().recordRestart(1063, &delay) != nbt::launcher::RestartDecision::Restart ||
            delay != 500) {
            return 1;
        }
        // Exhausted again, then the clock goes back (a reboot): counting restarts.
        if (value.value().recordRestart(1064, &delay) != nbt::launcher::RestartDecision::Restart ||
            value.value().recordRestart(1065, &delay) != nbt::launcher::RestartDecision::Restart ||
            value.value().recordRestart(1066, &delay) !=
                nbt::launcher::RestartDecision::Exhausted ||
            value.value().recordRestart(100, &delay) != nbt::launcher::RestartDecision::Restart ||
            delay != 500) {
            return 1;
        }
    }
    for (i = 0; i < (size_t)length; ++i) {
        if (!nbt_manager_state_parse(text, i, &parsed)) {
            return 1;
        }
    }
    memcpy(changed, text, (size_t)length + 1);
    {
        char *slot = strstr(changed, "slot=0\n");
        if (!slot) {
            return 1;
        }
        slot[5] = '9';
    }
    if (!nbt_manager_state_parse(changed, (size_t)length, &parsed) ||
        !nbt_manager_state_begin(&state, &v1, 0) || nbt_manager_state_begin(&state, &v2, 0) ||
        !state.pending || state.active.slot != 1 || state.previous.slot != 0 ||
        state.highwater != 1 || !nbt_manager_state_begin(&state, &v3, 0) ||
        !nbt_manager_state_confirm(&state, NULL, 0)) {
        return 1;
    }
    length = nbt_manager_state_format(&state, text, sizeof(text));
    if (length < 0 || nbt_manager_state_parse(text, (size_t)length, &parsed) || !parsed.pending ||
        parsed.active.sequence != 2 || parsed.previous.sequence != 1 ||
        nbt_manager_state_rollback(&parsed) || parsed.active.sequence != 1 ||
        parsed.highwater != 1 || parsed.pending || !nbt_manager_state_rollback(&parsed)) {
        return 1;
    }
    memcpy(changed, text, (size_t)length + 1);
    {
        char *slot = strstr(changed, "previous_slot=0\n");
        if (!slot) {
            return 1;
        }
        slot[14] = '1';
    }
    if (!nbt_manager_state_parse(changed, (size_t)length, &parsed)) {
        return 1;
    }
    memcpy(changed, text, (size_t)length + 1);
    {
        char *hash = strstr(changed, "sha256=");
        if (!hash) {
            return 1;
        }
        hash[7] = 'G';
    }
    if (!nbt_manager_state_parse(changed, (size_t)length, &parsed) ||
        nbt_manager_state_format(&state, changed, 10) >= 0) {
        return 1;
    }

    lease = mkstemp(path);
    if (lease < 0 || unlink(path) || pipe(ready) || pipe(stop)) {
        return 1;
    }
    child = fork();
    if (child < 0) {
        return 1;
    }
    if (!child) {
        struct flock lock;
        char byte;
        close(ready[0]);
        close(stop[1]);
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        if (fcntl(lease, F_SETLK, &lock) || write(ready[1], "R", 1) != 1 ||
            read(stop[0], &byte, 1) != 1) {
            _exit(1);
        }
        _exit(0);
    }
    close(ready[1]);
    close(stop[0]);
    {
        char byte;
        if (read(ready[0], &byte, 1) != 1) {
            return 1;
        }
    }
    memset(&process, 0, sizeof(process));
    process.pid = child;
    process.lease_fd = lease;
    process.ready = 1;
    process.ready_ms = 1000;
    process.last_heartbeat_ms = 31000;
    if (nbt_manager_state_trial(&state) || !nbt_manager_state_trial(&state) ||
        !nbt_manager_state_confirm(&state, &process, 30999) ||
        nbt_manager_state_confirm(&state, &process, 31000) || state.pending ||
        state.active.sequence != 2 || state.highwater != 2 ||
        !nbt_manager_state_begin(&state, &v1, 0) || nbt_manager_state_begin(&state, &v1, 1) ||
        state.previous.sequence != 2 || state.active.sequence != 1 ||
        nbt_manager_state_trial(&state) || nbt_manager_state_confirm(&state, &process, 31000) ||
        state.highwater != 2 || !nbt_manager_state_begin(&state, &v2, 0) ||
        nbt_manager_state_begin(&state, &v3, 0) || nbt_manager_state_rollback(&state) ||
        state.active.sequence != 1 || state.highwater != 2) {
        return 1;
    }
    if (write(stop[1], "X", 1) != 1 || waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status)) {
        return 1;
    }
    close(ready[0]);
    close(stop[1]);
    close(lease);
    return 0;
}

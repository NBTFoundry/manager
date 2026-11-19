#include "nbt/core/system.hpp"
#include "nbt/gui/worker_channel.hpp"
#include "nbt/gui/worker_client.hpp"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Reads one line from `fd` into `line` (without the newline). */
static bool readLine(int fd, char *line, size_t size) {
    size_t used = 0;
    char byte;
    while (used + 1 < size && read(fd, &byte, 1) == 1) {
        if (byte == '\n') {
            line[used] = 0;
            return true;
        }
        line[used++] = byte;
    }
    return false;
}

/* A worker that takes 3.5 s to answer SESSION, as the car's did while it
 * prepared Wi-Fi Direct (dev62): with the request on the channel's thread, the
 * UI loop keeps turning and the heartbeat may go on (the launcher stops a GUI
 * silent for 3 s). */
static int keepAlive() {
    int ends[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, ends)) {
        return 1;
    }
    const pid_t worker = fork();
    if (worker < 0) {
        return 1;
    }
    if (worker == 0) {
        char line[128];
        const struct timespec slow = {3, 500000000};
        close(ends[0]);
        if (!readLine(ends[1], line, sizeof(line)) || strcmp(line, "HELLO 1") ||
            write(ends[1], "HELLO\t1\n", 8) != 8) {
            _exit(1);
        }
        // The channel asks for the list when it starts and when idle.
        while (readLine(ends[1], line, sizeof(line)) && !strcmp(line, "LIST")) {
            if (write(ends[1], "APPS\t0\n", 7) != 7) {
                _exit(1);
            }
        }
        if (strcmp(line, "SESSION wfd0 8080")) {
            _exit(1);
        }
        nanosleep(&slow, NULL);
        _exit(write(ends[1], "SESSION\tready\n", 14) == 14 ? 0 : 1);
    }
    close(ends[1]);
    int status = 1;
    {
        nbt::gui::WorkerClient client;
        nbt::gui::WorkerChannel channel(client);
        if (!client.attach(ends[0]).ok() || !channel.start().ok()) {
            return 1;
        }
        const struct timespec tick = {0, 100000000};
        for (unsigned i = 0;; ++i) { // ready once the first LIST is answered, as the GUI
            const nbt::core::Result<uint64_t> now = nbt::core::monotonicMs();
            if (now.get() && channel.alive(now.value())) {
                break;
            }
            if (i > 20) {
                fprintf(stderr, "never ready\n");
                return 1;
            }
            nanosleep(&tick, NULL);
        }
        nbt::gui::Request request;
        snprintf(request.command, sizeof(request.command), "SESSION wfd0 8080");
        request.timeoutMs = 15000;
        if (!channel.submit(request) || channel.submit(request)) { // one at a time
            return 1;
        }
        char reply[64];
        char command[64];
        bool ok = false;
        unsigned turns = 0;
        while (!channel.take(reply, sizeof(reply), &ok, command, sizeof(command))) {
            const nbt::core::Result<uint64_t> now = nbt::core::monotonicMs();
            if (!now.get() || !channel.alive(now.value()) || ++turns > 100) {
                fprintf(stderr, "not alive after %u turns\n", turns);
                return 1;
            }
            nanosleep(&tick, NULL);
        }
        if (!ok || strcmp(reply, "SESSION\tready") || strcmp(command, "SESSION wfd0 8080") ||
            turns < 30) {
            fprintf(stderr, "reply %s after %u turns\n", reply, turns);
            return 1;
        }
        channel.stop();
        status = 0;
    }
    int exited = 0;
    return waitpid(worker, &exited, 0) == worker && WIFEXITED(exited) && !WEXITSTATUS(exited)
               ? status
               : 1;
}

int main(int argc, char **argv) {
    nbt::gui::WorkerClient client;
    char reply[512];
    if (argc == 2 && !strcmp(argv[1], "keepalive")) {
        return keepAlive();
    }
    if (argc != 3) {
        return 2;
    }
    if (!client.spawnTest(argv[1], argv[2]).ok()) {
        perror("spawn");
        return 1;
    }
    if (!client.request("LIST", nbt::core::Span<char>(reply, sizeof(reply)), 5000).ok() ||
        strcmp(reply, "APPS\t1\ttest") ||
        !client.request("SESSION test lo 0", nbt::core::Span<char>(reply, sizeof(reply)), 5000)
             .ok() ||
        strncmp(reply, "SESSION\t", 8) ||
        !client.request("STATUS", nbt::core::Span<char>(reply, sizeof(reply)), 5000).ok() ||
        strncmp(reply, "STATUS\t", 7) ||
        !client.request("CANCEL", nbt::core::Span<char>(reply, sizeof(reply)), 5000).ok() ||
        strcmp(reply, "OK")) {
        perror("request");
        return 1;
    }
    return 0;
}

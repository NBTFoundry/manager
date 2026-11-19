#include "phone_display.hpp"
#include "nbt/process/hmi_environment.hpp"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
namespace nbt {
namespace plugin {
void PhoneDisplayProbe::reset() {
    if (fd >= 0) {
        close(fd);
    }
    fd = -1;
    used = 0;
    if (pid > 0) {
        int status = 0;
        const pid_t reaped = waitpid(pid, &status, WNOHANG);
        if (!reaped) {
            // Keep ownership until reaped: its PID cannot be recycled under us.
            (void)kill(pid, SIGKILL);
        } else if (reaped == pid || (reaped < 0 && errno == ECHILD)) {
            pid = 0;
        }
    }
}
void PhoneDisplayProbe::stop() {
    reset();
    // Finalization only. Polling and starting another probe never block.
    if (pid > 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        pid = 0;
    }
}
bool PhoneDisplayProbe::start(uint64_t now) {
    reset();
    if (pid > 0) {
        return false;
    }
    int ends[2];
    if (pipe(ends)) {
        return false;
    }
    const int nullfd = open("/dev/null", O_RDWR);
    if (nullfd < 0 || fcntl(ends[0], F_SETFD, FD_CLOEXEC) || fcntl(ends[1], F_SETFD, FD_CLOEXEC) ||
        fcntl(ends[0], F_SETFL, O_NONBLOCK)) {
        close(ends[0]);
        close(ends[1]);
        if (nullfd >= 0) {
            close(nullfd);
        }
        return false;
    }
    struct inheritance inherit;
    memset(&inherit, 0, sizeof(inherit));
    inherit.flags = SPAWN_SETSIGDEF | SPAWN_SETSIGMASK;
    sigfillset(&inherit.sigdefault);
    sigemptyset(&inherit.sigmask);
    const char path[] = "/opt/sys/bin/LayerManagerControl";
    char *argv[] = {const_cast<char *>(path), const_cast<char *>("get"),
                    const_cast<char *>("layer"), const_cast<char *>("0x80000000"), NULL};
    char *env[process::kHmiEnvironmentMax];
    if (!process::hmiEnvironment(env, NULL, 0, false)) {
        close(nullfd);
        close(ends[0]);
        close(ends[1]);
        return false;
    }
    const int map[] = {nullfd, ends[1], nullfd};
    const pid_t child = spawn(path, 3, map, &inherit, argv, env);
    close(nullfd);
    close(ends[1]);
    if (child < 0) {
        close(ends[0]);
        return false;
    }
    fd = ends[0];
    pid = static_cast<int>(child);
    started = now;
    used = 0;
    output[0] = 0;
    return true;
}
int PhoneDisplayProbe::poll(uint64_t now) {
    if (fd < 0 || now < started || now - started > 2000 || used == sizeof(output) - 1) {
        reset();
        return 0;
    }
    const ssize_t n = read(fd, output + used, sizeof(output) - 1 - used);
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
        return -1;
    }
    if (n > 0) {
        used += static_cast<size_t>(n);
        output[used] = 0;
        return -1;
    }
    if (n < 0) {
        reset();
        return 0;
    }
    const char *p = strstr(output, "surface render order:");
    if (p) {
        p += 21;
        p += strspn(p, " \t");
        if (!strncmp(p, "256(0x100)", 10)) {
            p += 10;
            p += strspn(p, " ,\t\r");
            const bool alone = *p == '\n' || !*p;
            reset();
            return alone ? 1 : 0;
        }
    }
    reset();
    return 0;
}
} // namespace plugin
} // namespace nbt

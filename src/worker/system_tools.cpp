/* The head unit's own tools, run with fixed arguments and no shell, as the
 * installer runs them over SSH: `mount -uw|-ur /fs/sda0` around a plugin
 * replacement (NBT-AAD's pf_aa remounts it the same way from an iDrive-started
 * process), `OnOffDSICommander appreset`, which restarts iDrive, and
 * `sloginfo`, which prints the system log for a paired phone. All are
 * looked up on the PATH iDrive passes on (the HMI's, with AAD's additions),
 * then in the folders the car inventory found them in. */
#include "nbt/log.h"
#include "nbt/manager_refresh.h"
#include "nbt/process/child_spawn.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

namespace {

#ifdef NBT_TESTING
/* Host tests: record the call in NBT_TEST_SYSTEM_LOG; NBT_TEST_SYSTEM_FAIL
 * names a call that fails. Nothing is run. */
int testCall(const char *what) {
    const char *path = getenv("NBT_TEST_SYSTEM_LOG");
    const char *failing = getenv("NBT_TEST_SYSTEM_FAIL");
    FILE *log = path ? fopen(path, "a") : NULL;
    if (log) {
        fprintf(log, "%s\n", what);
        fclose(log);
    }
    return failing && !strcmp(failing, what) ? -1 : 0;
}
#else
const char *const kRestartFolders[] = {"/opt/core/bin", "/opt/car/bin"};
const char *const kMountFolders[] = {"/bin", "/sbin", "/usr/bin", "/proc/boot"};

/* An executable `name` in an absolute PATH entry, else in `folders`. */
bool find(const char *name, const char *const *folders, size_t count, char *out, size_t size) {
    const char *path = getenv("PATH");
    while (path && *path) {
        const char *end = strchr(path, ':');
        const size_t length = end ? static_cast<size_t>(end - path) : strlen(path);
        if (length && path[0] == '/' &&
            snprintf(out, size, "%.*s/%s", static_cast<int>(length), path, name) <
                static_cast<int>(size) &&
            !access(out, X_OK)) {
            return true;
        }
        path = end ? end + 1 : NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (snprintf(out, size, "%s/%s", folders[i], name) < static_cast<int>(size) &&
            !access(out, X_OK)) {
            return true;
        }
    }
    return false;
}

int run(const char *name, const char *const *folders, size_t count, char *const argv[], bool wait,
        unsigned seconds) {
    char path[256];
    pid_t pid = -1;
    const int fds[3] = {-1, -1, 2}; // errors go to the worker's stderr: the debug log
    if (!find(name, folders, count, path, sizeof(path))) {
        nbt_logf("%s not found", name);
        return -1;
    }
    const int error = nbt::process::spawnChild(path, argv, environ, fds, 3, &pid);
    if (error) {
        nbt_logf("%s: cannot start (errno %d)", path, error);
        return -1;
    }
    if (!wait) {
        return 0;
    }
    int status = 0;
    for (unsigned tenth = 0; tenth < seconds * 10; ++tenth) {
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            if (WIFEXITED(status) && !WEXITSTATUS(status)) {
                return 0;
            }
            nbt_logf("%s exited with status %d", path,
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            return -1;
        }
        if (done < 0 && errno != EINTR) {
            return -1;
        }
        const struct timespec pause = {0, 100000000};
        nanosleep(&pause, NULL);
    }
    nbt_logf("%s did not finish in %u s", path, seconds);
    return -1;
}
#endif

} // namespace

int nbt_system_log_start(int out_fd, pid_t *pid) {
    char tool[] = "sloginfo";
    char *const argv[] = {tool, NULL};
    const int fds[3] = {-1, out_fd, -1};
#ifdef NBT_TESTING
    const char *fake = getenv("NBT_TEST_SLOGINFO");
    if (!fake) {
        errno = ENOENT;
        return -1;
    }
    const int error = nbt::process::spawnChild(fake, argv, environ, fds, 3, pid);
#else
    char path[256];
    if (!find(tool, kMountFolders, sizeof(kMountFolders) / sizeof(kMountFolders[0]), path,
              sizeof(path))) {
        nbt_logf("sloginfo not found");
        errno = ENOENT;
        return -1;
    }
    const int error = nbt::process::spawnChild(path, argv, environ, fds, 3, pid);
#endif
    if (error) {
        errno = error;
        return -1;
    }
    return 0;
}

int nbt_system_remount(int writable) {
#ifdef NBT_TESTING
    return testCall(writable ? "remount rw" : "remount ro");
#else
    char mount[] = "mount", option[] = "-uw", point[] = "/fs/sda0";
    option[2] = writable ? 'w' : 'r';
    char *const argv[] = {mount, option, point, NULL};
    nbt_logf("%s /fs/sda0", writable ? "mount -uw" : "mount -ur");
    return run("mount", kMountFolders, sizeof(kMountFolders) / sizeof(kMountFolders[0]), argv, true,
               30);
#endif
}

int nbt_system_restart_idrive(void) {
#ifdef NBT_TESTING
    return testCall("restart idrive");
#else
    char tool[] = "OnOffDSICommander", action[] = "appreset";
    char *const argv[] = {tool, action, NULL};
    nbt_logf("restarting the head unit (OnOffDSICommander appreset)");
    return run(tool, kRestartFolders, sizeof(kRestartFolders) / sizeof(kRestartFolders[0]), argv,
               false, 0);
#endif
}

static bool hasCapabilityToken(const char *output, size_t length, const char *format) {
    const size_t flen = strlen(format);
    if (!flen || strncmp(output, "formats=", 8)) {
        return false;
    }
    const char *p = output + 8;
    const char *end = output + length;
    while (p < end && *p && *p != '\n' && *p != '\r') {
        const char *comma = static_cast<const char *>(memchr(p, ',', static_cast<size_t>(end - p)));
        const char *eol = static_cast<const char *>(memchr(p, '\n', static_cast<size_t>(end - p)));
        const char *tokEnd = comma && (!eol || comma < eol) ? comma : (eol ? eol : end);
        if (static_cast<size_t>(tokEnd - p) == flen && !memcmp(p, format, flen)) {
            return true;
        }
        if (!comma || (eol && eol < comma)) {
            break;
        }
        p = comma + 1;
    }
    return false;
}

int nbt_manager_launcher_supports_format(nbt_fs *fs, const char *format) {
    char path[NBT_PATH_MAX];
    if (!fs || !format || !*format) {
        return -1;
    }
    const int check = nbt_fs_open(fs, NBT_FS_SETUP_LAUNCHER, "manager", NULL, NBT_FS_READ);
    if (check < 0) {
        return 0;
    }
    close(check);
    if (snprintf(path, sizeof(path), "%s/manager/nbt-manager-launcher", fs->root) >=
        static_cast<int>(sizeof(path))) {
        return -1;
    }
    int fds[2];
    if (pipe(fds)) {
        return -1;
    }
    (void)fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    char flag[] = "--capabilities";
    char *const argv[] = {path, flag, NULL};
    const int childFds[3] = {-1, fds[1], -1};
    pid_t pid = -1;
    const int spawnErr = nbt::process::spawnChild(path, argv, environ, childFds, 3, &pid);
    close(fds[1]);
    if (spawnErr) {
        close(fds[0]);
        return 0;
    }
    char buf[256];
    size_t used = 0;
    while (used < sizeof(buf) - 1) {
        const ssize_t n = read(fds[0], buf + used, sizeof(buf) - 1 - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        used += static_cast<size_t>(n);
    }
    close(fds[0]);
    buf[used] = 0;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return 0;
    }
    return hasCapabilityToken(buf, used, format) ? 1 : 0;
}

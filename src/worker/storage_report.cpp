/* Where the manager's root really is, once per worker start in the debug log.
 * The car's `mount` output (nbt-inventory-5) shows no /var, only /var/pps, so
 * which filesystem holds /var/opt/nbt-manager, and how much room it has, is
 * not known: this line is the car's own answer. The QNX 6.5 VM only showed
 * that the calls work: devctl(DCMD_FSYS_MOUNTED_ON/_AT) on a descriptor of a
 * qnx6 directory names its device and mount point, statvfs its type, and on
 * /dev/shmem both fail (ENOTTY), so each failure is logged as such. The VM
 * says nothing about the car's layout. */
#include "nbt/worker/storage_report.hpp"

#include "nbt/log.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#ifdef __QNX__
#include <devctl.h>
#include <sys/dcmd_blk.h>
#endif

namespace nbt {
namespace worker {
namespace {

void describeVar(char *out, size_t size) {
    struct stat st;
    char target[256];
    if (lstat("/var", &st)) {
        snprintf(out, size, "/var: lstat errno %d", errno);
    } else if (S_ISLNK(st.st_mode)) {
        const ssize_t n = readlink("/var", target, sizeof(target) - 1);
        target[n > 0 ? n : 0] = 0;
        snprintf(out, size, "/var is a link to %s", n > 0 ? target : "?");
    } else {
        snprintf(out, size, "/var is a %s", S_ISDIR(st.st_mode) ? "directory" : "file");
    }
}

void describeMount(const char *root, char *out, size_t size) {
#ifdef __QNX__
    char on[256] = "", at[256] = "";
    const int fd = open(root, O_RDONLY);
    if (fd < 0) {
        snprintf(out, size, "open errno %d", errno);
        return;
    }
    const int onError = devctl(fd, DCMD_FSYS_MOUNTED_ON, on, sizeof(on), NULL);
    const int atError = devctl(fd, DCMD_FSYS_MOUNTED_AT, at, sizeof(at), NULL);
    close(fd);
    on[sizeof(on) - 1] = at[sizeof(at) - 1] = 0;
    char onText[32], atText[32];
    snprintf(onText, sizeof(onText), "? (errno %d)", onError);
    snprintf(atText, sizeof(atText), "? (errno %d)", atError);
    snprintf(out, size, "on %s at %s", onError ? onText : on, atError ? atText : at);
#else
    (void)root;
    snprintf(out, size, "on ? at ?");
#endif
}

} // namespace

void logStorage(const char *root) {
    char mount[600], var[300], real[PATH_MAX];
    struct statvfs vfs;
    describeMount(root, mount, sizeof(mount));
    describeVar(var, sizeof(var));
    if (!realpath(root, real)) {
        snprintf(real, sizeof(real), "? (errno %d)", errno);
    }
    if (statvfs(root, &vfs)) {
        nbt_logf("storage %s: %s, real path %s; statvfs errno %d; %s", root, mount, real, errno,
                 var);
        return;
    }
    const uint64_t unit = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
    const unsigned long long freeMiB =
        static_cast<unsigned long long>((static_cast<uint64_t>(vfs.f_bavail) * unit) >> 20);
    const unsigned long long totalMiB =
        static_cast<unsigned long long>((static_cast<uint64_t>(vfs.f_blocks) * unit) >> 20);
#ifdef __QNX__
    const char *type = vfs.f_basetype;
#else
    const char *type = "?";
#endif
    nbt_logf("storage %s: %.15s %s, real path %s; %llu MiB free of %llu MiB%s; %s", root, type,
             mount, real, freeMiB, totalMiB, (vfs.f_flag & ST_RDONLY) ? ", read-only" : "", var);
}

} // namespace worker
} // namespace nbt

#include "nbt/fs.h"
#include "nbt/log.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static char root[256], dir[300], path[320], old[320];

static long long size(const char *p) {
    struct stat st;
    return stat(p, &st) ? -1 : (long long)st.st_size;
}

static size_t slurp(const char *p, char *out, size_t capacity) {
    FILE *f = fopen(p, "rb");
    size_t n;
    CHECK(f);
    n = fread(out, 1, capacity - 1, f);
    CHECK(!ferror(f) && fclose(f) == 0);
    out[n] = 0;
    return n;
}

static void create(const char *p, mode_t mode) {
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0 && fchmod(fd, mode) == 0 && close(fd) == 0);
}

/* Every line: "<digits>.<3 digits> <component>[<pid>] <printable text>\n". */
static size_t check_lines(const char *text, size_t n, const char *component) {
    size_t lines = 0, i = 0;
    while (i < n) {
        const char *line = text + i;
        const char *end = static_cast<const char *>(memchr(line, '\n', n - i));
        size_t length, j = 0, c = strlen(component);
        CHECK(end);
        length = (size_t)(end - line);
        while (j < length && line[j] >= '0' && line[j] <= '9') {
            ++j;
        }
        CHECK(j > 0 && j + 5 < length && line[j] == '.');
        CHECK(line[j + 1] >= '0' && line[j + 1] <= '9' && line[j + 3] >= '0' && line[j + 3] <= '9');
        CHECK(line[j + 4] == ' ');
        if (component[0]) {
            CHECK(length > j + 5 + c && !memcmp(line + j + 5, component, c) &&
                  line[j + 5 + c] == '[');
        }
        for (j = 0; j < length; ++j) {
            CHECK(line[j] >= 0x20 && line[j] <= 0x7e);
        }
        CHECK(length <= NBT_LOG_LINE + 48);
        ++lines;
        i += length + 1;
    }
    return lines;
}

static void test_disabled_and_refused(nbt_fs *fs) {
    char text[64];
    struct stat st;
    CHECK(nbt_log_open(fs, "gui") == 0 && !nbt_log_enabled());
    nbt_logf("nothing");
    CHECK(lstat(path, &st) == -1 && errno == ENOENT); /* never created to switch logging on */
    CHECK(symlink(old, path) == 0);
    create(old, 0600);
    CHECK(nbt_log_open(fs, "gui") == -1 && !nbt_log_enabled());
    CHECK(nbt_log_open_path(path, "plugin") == -1 && !nbt_log_enabled());
    CHECK(unlink(path) == 0);
    CHECK(link(old, path) == 0); /* two links */
    CHECK(nbt_log_open(fs, "gui") == -1 && nbt_log_open_path(path, "plugin") == -1);
    CHECK(unlink(path) == 0 && unlink(old) == 0);
    create(path, 0620); /* group-writable */
    CHECK(nbt_log_open(fs, "gui") == -1 && nbt_log_open_path(path, "plugin") == -1);
    CHECK(chmod(path, 0602) == 0); /* world-writable */
    CHECK(nbt_log_open(fs, "gui") == -1 && nbt_log_open_path(path, "plugin") == -1);
    CHECK(unlink(path) == 0 && mkdir(path, 0700) == 0); /* not a file */
    CHECK(nbt_log_open(fs, "gui") == -1 && nbt_log_open_path(path, "plugin") == -1);
    CHECK(rmdir(path) == 0);
    create(path, 0600);
    CHECK(nbt_log_open(fs, "bad name") == -1 && nbt_log_open(fs, "") == -1 &&
          nbt_log_open(fs, "sixteencharacter") == -1 && nbt_log_open(NULL, "gui") == -1);
    CHECK(nbt_log_open_path("relative/debug.log", "plugin") == -1 &&
          nbt_log_open_path(NULL, "plugin") == -1);
    CHECK(!nbt_log_enabled());
    CHECK(slurp(path, text, sizeof(text)) == 0);
}

static void test_lines(nbt_fs *fs) {
    char text[4096], expected[64], *message;
    char long_message[NBT_LOG_LINE * 3];
    CHECK(nbt_log_open(fs, "gui") == 1 && nbt_log_enabled());
    CHECK(nbt_log_open(fs, "gui") == 1); /* already on */
    nbt_logf("hello %s", "a\nb\r\x01\x7f\xc3\xa9z");
    memset(long_message, 'x', sizeof(long_message) - 1);
    long_message[sizeof(long_message) - 1] = 0;
    nbt_logf("%s", long_message);
    CHECK(check_lines(text, slurp(path, text, sizeof(text)), "gui") == 2);
    snprintf(expected, sizeof(expected), " gui[%ld] hello a?b???", (long)getpid());
    message = strstr(text, expected);
    CHECK(message && !strncmp(message + strlen(expected), "??z\n", 4));
    message = strchr(text, '\n') + 1;
    message = strchr(message, ']') + 2;
    CHECK(strspn(message, "x") == NBT_LOG_LINE && message[NBT_LOG_LINE] == '\n');
    nbt_log_close();
    CHECK(!nbt_log_enabled());
    nbt_logf("after close");
    CHECK(!strstr((slurp(path, text, sizeof(text)), text), "after close"));
}

/* Fills the log in this process and in a child at the same time. */
static void test_rotation(nbt_fs *fs) {
    static char text[NBT_LOG_MAX + 4096];
    pid_t child;
    int status, i;
    size_t lines;
    CHECK(unlink(path) == 0);
    create(path, 0600);
    child = fork();
    CHECK(child >= 0);
    CHECK(nbt_log_open(fs, child ? "parent" : "child") == 1);
    for (i = 0; i < 6000; ++i) {
        nbt_logf("line %d of the rotation test with some padding text", i);
    }
    nbt_log_close();
    if (!child) {
        _exit(0);
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(size(path) > 0);
    /* Writers check the size separately, so each may add one line past it. */
    CHECK(size(path) <= (long long)(NBT_LOG_MAX + 2 * (NBT_LOG_LINE + 64)));
    CHECK(size(old) > (long long)NBT_LOG_MAX - 200 &&
          size(old) <= (long long)(NBT_LOG_MAX + 2 * (NBT_LOG_LINE + 64)));
    lines = check_lines(text, slurp(path, text, sizeof(text)), "");
    lines += check_lines(text, slurp(old, text, sizeof(text)), "");
    /* At least the rotated file's worth: lines are about 80 bytes, longer as the
     * clock prefix and process ids grow (2000 failed at 620763 s of uptime). */
    CHECK(lines > NBT_LOG_MAX / 100);
    /* The newest lines are in the current file. */
    CHECK(strstr((slurp(path, text, sizeof(text)), text), "line 5999 "));
}

/* The plugin never rotates; it drops lines while the file is full and follows
 * a file rotated by the GUI or the worker. */
static void test_plain(void) {
    static char text[NBT_LOG_MAX + 4096];
    long long before_old = size(old);
    int i;
    CHECK(nbt_log_open_path(path, "plugin") == 1);
    CHECK(nbt_log_open_path(path, "plugin") == 1);
    for (i = 0; i < 4000; ++i) {
        nbt_logf("plugin line %d with padding to fill the file faster", i);
    }
    CHECK(size(path) <= (long long)NBT_LOG_MAX && size(path) > (long long)NBT_LOG_MAX - 200);
    CHECK(size(old) == before_old);
    check_lines(text, slurp(path, text, sizeof(text)), "");
    const unsigned file = nbt_log_file();
    CHECK(file != 0 && nbt_log_file() == file);
    CHECK(rename(path, old) == 0);
    create(path, 0600);
    /* Switched off and on (or rotated) elsewhere: the next file is noticed
     * before any line, so the plugin can log its context first. */
    CHECK(nbt_log_file() == file + 1);
    nbt_logf("after rotation");
    CHECK(check_lines(text, slurp(path, text, sizeof(text)), "plugin") == 1 &&
          strstr(text, "after rotation"));
    /* Removing the file switches logging off. */
    CHECK(unlink(path) == 0);
    nbt_logf("after removal");
    CHECK(!nbt_log_enabled() && nbt_log_file() == 0);
    create(path, 0600);
    nbt_log_close();
}

/* Writes that fail (a full disk) drop lines without stopping the caller. */
static void test_full_disk(nbt_fs *fs) {
    static char text[NBT_LOG_MAX + 4096];
    struct rlimit saved, limit;
    long long start;
    int i;
    CHECK(unlink(path) == 0);
    create(path, 0600);
    CHECK(nbt_log_open(fs, "worker") == 1);
    nbt_logf("before full");
    start = size(path);
    CHECK(signal(SIGXFSZ, SIG_IGN) != SIG_ERR && getrlimit(RLIMIT_FSIZE, &saved) == 0);
    limit = saved;
    limit.rlim_cur = (rlim_t)start + 10;
    CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0);
    for (i = 0; i < 50; ++i) {
        nbt_logf("while full %d", i);
    }
    CHECK(setrlimit(RLIMIT_FSIZE, &saved) == 0);
    CHECK(size(path) <= start + 10);
    nbt_logf("after full");
    slurp(path, text, sizeof(text));
    CHECK(strstr(text, "before full") && strstr(text, "after full\n"));
    nbt_log_close();
}

int main(void) {
    nbt_fs fs;
    char error[128];
    snprintf(root, sizeof(root), "%s/nbt-log-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(root) && chmod(root, 0700) == 0);
    snprintf(dir, sizeof(dir), "%s/manager", root);
    snprintf(path, sizeof(path), "%s/debug.log", dir);
    snprintf(old, sizeof(old), "%s/debug.log.1", dir);
    CHECK(nbt_fs_init(&fs, root, error, sizeof(error)) == 0);
    /* Without the manager directory there is nothing to open. */
    CHECK(nbt_log_open(&fs, "gui") == 0);
    CHECK(mkdir(dir, 0700) == 0);
    test_disabled_and_refused(&fs);
    test_lines(&fs);
    test_rotation(&fs);
    test_plain();
    test_full_disk(&fs);
    unlink(path);
    unlink(old);
    rmdir(dir);
    rmdir(root);
    return 0;
}

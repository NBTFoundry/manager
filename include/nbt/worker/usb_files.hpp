// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_USB_FILES_HPP
#define NBT_WORKER_USB_FILES_HPP

/* Files for an application from a USB stick (capability `files`, chosen on
 * the car: ipc_files.cpp). scan() and import() run on the worker's job
 * thread (Task::FilesScan, Task::FilesImport); the loop reads progress()
 * meanwhile, and the matches once the task ended.
 *
 * Sticks are the /mnt/umass* folders (NBT_TEST_USB_MOUNTS in test builds),
 * walked to depth 4, at most 4096 entries in all, without following links or
 * entering hidden folders. A file matches when its name, made a valid stored
 * name (bytes it may not hold become '_'), has an extension the application
 * accepts. Each is copied through the files root's staging and admission
 * checks, as a phone's file is. */

#include "nbt/core/mutex.hpp"
#include "nbt/package.h"
#include "nbt/worker/app_files.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace worker {

struct UsbMatch {
    char path[256];
    char name[NBT_FILES_NAME_MAX + 1];
    uint64_t size;
    bool stored;
};

struct UsbProgress {
    unsigned doneFiles, totalFiles;
    uint64_t doneBytes, totalBytes;
};

class UsbFiles {
public:
    /* Set by the loop: begin() as a task starts, end() once it ended. */
    enum class Phase { Idle, Scanning, Scanned, Importing, Imported, Failed };
    static const unsigned kMatches = 64;
    static const unsigned kEntriesMax = 4096;
    static const unsigned kDepthMax = 4;
    /* Every match (-1) or one. */
    static const int kAll = -1;

    UsbFiles();
    UsbFiles(const UsbFiles &) = delete;
    UsbFiles &operator=(const UsbFiles &) = delete;

    /* The loop, before a task: whose files, and which to import. */
    void prepare(const char *app, const AppFiles &files);
    bool select(int index);
    const char *app() const {
        return app_;
    }

    void begin(bool importing) {
        phase_ = importing ? Phase::Importing : Phase::Scanning;
    }
    void end(int result) {
        phase_ = result                       ? Phase::Failed
                 : phase_ == Phase::Importing ? Phase::Imported
                                              : Phase::Scanned;
    }
    Phase phase() const {
        return phase_;
    }
    bool running() const {
        return phase_ == Phase::Scanning || phase_ == Phase::Importing;
    }

    /* The job's thread. 0, or -1 with reason() set. */
    int scan();
    int import();

    /* The loop, once the task ended (matches) or any time (progress). */
    unsigned count() const {
        return count_;
    }
    const UsbMatch &match(unsigned i) const {
        return matches_[i];
    }
    /* Whether match `i` is stored now (also while an import runs). */
    bool stored(unsigned i);
    UsbProgress progress();
    const char *reason() const {
        return reason_;
    }

    /* `name` as a stored name: bytes a name may not hold become '_', leading
     * and trailing dots and spaces go, a long name keeps its extension.
     * False when nothing is left. */
    static bool storedName(const char *name, char out[NBT_FILES_NAME_MAX + 1]);

private:
    void walk(char *path, size_t length, unsigned depth, unsigned &entries);
    int copy(UsbMatch &match);
    void advance(uint64_t bytes, bool file);

    char app_[NBT_ID_MAX + 1];
    Phase phase_;
    AppFiles files_;
    UsbMatch matches_[kMatches];
    unsigned count_;
    int selected_;
    char reason_[16];
    core::Mutex mutex_; // progress_ and stored flags
    UsbProgress progress_;
    unsigned char buffer_[16384];
};

} // namespace worker
} // namespace nbt

#endif

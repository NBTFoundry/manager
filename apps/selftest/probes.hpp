// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_SELFTEST_PROBES_HPP
#define NBT_SELFTEST_PROBES_HPP

#include "report.hpp"

#include "nbt/app/car_data.hpp"
#include "nbt/app/diag.hpp"
#include "nbt/app/lifecycle.hpp"

#include <pthread.h>
#include <stdint.h>

namespace selftest {

/* `bridge`: every capability in `declared` (the package's, comma separated)
 * granted, except `files`, which needs the hard disk (its own check). */
void checkBridge(const nbt::app::Lifecycle &lifecycle, const char *declared, Report &report);
/* `files`: the files folder listed; skipped without one. */
void checkFiles(const nbt::app::Lifecycle &lifecycle, Report &report);

/* `cardata`: a value of the speed or the outside temperature within 5 s. */
class CarDataProbe {
public:
    CarDataProbe() : started_(0), done_(false) {
    }
    void start(uint64_t nowMs, Report &report);
    void poll(uint64_t nowMs, Report &report);
    bool done() const {
        return done_;
    }

private:
    nbt::app::CarData data_;
    uint64_t started_;
    bool done_;
};

/* `diag`: the engine computer (DME, 0x12) answers a read of its active
 * session (22 F186: nothing about the car or its owner), on a thread of its
 * own: a request blocks up to its timeout. */
class DiagProbe {
public:
    DiagProbe();
    ~DiagProbe();
    void start(Report &report);
    /* True once the answer (or none) is in the report. */
    bool poll(Report &report);

private:
    DiagProbe(const DiagProbe &);
    DiagProbe &operator=(const DiagProbe &);
    static void *run(void *self);
    nbt::app::Diag diag_;
    pthread_t thread_;
    bool running_, reported_;
    volatile int finished_;
    nbt::app::Diag::Status status_;
    uint8_t nrc_;
};

} // namespace selftest

#endif

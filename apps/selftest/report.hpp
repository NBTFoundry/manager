// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_SELFTEST_REPORT_HPP
#define NBT_SELFTEST_REPORT_HPP

#include <stddef.h>

namespace selftest {

enum Result { kPass, kFail, kSkip };

/* What NBT Manager reads when the self test ends (docs/self-test.md):
 * "SELFTEST1\t<form>", then "<check>\t<pass|fail|skip>\t<detail>" lines. */
class Report {
public:
    enum { kLines = 16, kDetailMax = 40 };
    explicit Report(const char *form);
    /* A check's result; a second one for the same check replaces it. The
     * detail is cut to kDetailMax printable characters. */
    void set(const char *check, Result result, const char *format, ...)
        __attribute__((format(printf, 4, 5)));
    bool has(const char *check) const;
    size_t count(Result result) const;
    /* <dir>/selftest.txt, written to a temporary file renamed over it. */
    bool write(const char *dir) const;

private:
    struct Line {
        char check[13];
        Result result;
        char detail[kDetailMax + 1];
    };
    const char *form_;
    Line lines_[kLines];
    size_t count_;
};

} // namespace selftest

#endif

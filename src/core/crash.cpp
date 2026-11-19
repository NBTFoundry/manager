// SPDX-License-Identifier: MPL-2.0
#include "nbt/core/crash.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#ifdef __QNX__
#include <ucontext.h>
#else
#include <sys/ucontext.h>
#endif

namespace nbt {
namespace core {
namespace {

CrashConfig configuration = {NULL, NULL, NULL};
#ifndef __QNX__
uint64_t alternateStack[16384 / sizeof(uint64_t)];
#endif

size_t appendText(char *output, size_t size, size_t offset, const char *text) {
    if (!text) {
        text = "unknown";
    }
    while (*text && offset + 1 < size) {
        output[offset++] = *text++;
    }
    return offset;
}

size_t appendHex(char *output, size_t size, size_t offset, uintptr_t value) {
    static const char digits[] = "0123456789abcdef";
    char reversed[2 * sizeof(uintptr_t)];
    size_t count = 0;
    do {
        reversed[count++] = digits[value & 15];
        value >>= 4;
    } while (value && count < sizeof(reversed));
    while (count && offset + 1 < size) {
        output[offset++] = reversed[--count];
    }
    return offset;
}

size_t appendUnsigned(char *output, size_t size, size_t offset, unsigned long value) {
    char reversed[32];
    size_t count = 0;
    do {
        reversed[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value && count < sizeof(reversed));
    while (count && offset + 1 < size) {
        output[offset++] = reversed[--count];
    }
    return offset;
}

void writeRecord(int signalNumber, void *address, uintptr_t returnAddress, const char *path,
                 bool create) {
    if (!path) {
        return;
    }
    char record[512];
    size_t offset = 0;
    offset = appendText(record, sizeof(record), offset, "crash component=");
    offset = appendText(record, sizeof(record), offset, configuration.component);
    offset = appendText(record, sizeof(record), offset, " pid=");
    offset = appendUnsigned(record, sizeof(record), offset, static_cast<unsigned long>(getpid()));
    offset = appendText(record, sizeof(record), offset, " signal=");
    offset =
        appendUnsigned(record, sizeof(record), offset, static_cast<unsigned long>(signalNumber));
    offset = appendText(record, sizeof(record), offset, " address=0x");
    offset = appendHex(record, sizeof(record), offset, reinterpret_cast<uintptr_t>(address));
    offset = appendText(record, sizeof(record), offset, " return=0x");
    offset = appendHex(record, sizeof(record), offset, returnAddress);
    if (offset + 1 < sizeof(record)) {
        record[offset++] = '\n';
    }
    const int flags = O_WRONLY | O_APPEND | (create ? O_CREAT : 0);
    const int fd = open(path, flags, 0600);
    if (fd < 0) {
        return;
    }
    size_t written = 0;
    while (written < offset) {
        const ssize_t count = write(fd, record + written, offset - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        written += static_cast<size_t>(count);
    }
    close(fd);
}

void crashHandler(int signalNumber, siginfo_t *info, void *context) {
    uintptr_t returnAddress = 0;
#if defined(__linux__) && defined(__x86_64__)
    ucontext_t *machine = static_cast<ucontext_t *>(context);
    returnAddress = static_cast<uintptr_t>(machine->uc_mcontext.gregs[REG_RIP]);
#elif defined(__QNX__) && defined(__ARM__)
    ucontext_t *machine = static_cast<ucontext_t *>(context);
    returnAddress = static_cast<uintptr_t>(machine->uc_mcontext.cpu.gpr[ARM_REG_PC]);
#else
    (void)context;
#endif
    writeRecord(signalNumber, info ? info->si_addr : NULL, returnAddress, configuration.markerPath,
                true);
    if (configuration.debugPath && access(configuration.debugPath, F_OK) == 0) {
        writeRecord(signalNumber, info ? info->si_addr : NULL, returnAddress,
                    configuration.debugPath, false);
    }
    signal(signalNumber, SIG_DFL);
    raise(signalNumber);
}

} // namespace

Status installCrashHandlers(const CrashConfig &config) {
    if (!config.component || !config.markerPath) {
        return Status::error(EINVAL, "invalid crash configuration");
    }
    configuration = config;
#ifndef __QNX__
    stack_t stack;
    memset(&stack, 0, sizeof(stack));
    stack.ss_sp = alternateStack;
    stack.ss_size = sizeof(alternateStack);
    if (sigaltstack(&stack, NULL)) {
        return Status::fromErrno("install alternate signal stack failed");
    }
#endif
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = &crashHandler;
#ifdef __QNX__
    action.sa_flags = SA_SIGINFO | SA_NODEFER;
#else
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
#endif
    if (sigemptyset(&action.sa_mask)) {
        return Status::fromErrno("crash signal mask failed");
    }
    const int signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        if (sigaction(signals[i], &action, NULL)) {
            return Status::fromErrno("install crash handler failed");
        }
    }
    return Status();
}

} // namespace core
} // namespace nbt

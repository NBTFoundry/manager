// SPDX-License-Identifier: MPL-2.0
#include "nbt/core/checked.hpp"
#include "nbt/core/fault.hpp"
#include "nbt/core/mutex.hpp"
#include "nbt/core/scope_exit.hpp"
#include "nbt/core/secure.hpp"
#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/core/text.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/model/bridge.hpp"
#include "nbt/process/hmi_environment.hpp"
#include "nbt/sdk/settings.hpp"
#include "nbt/storage/managed_root.hpp"
#include "nbt/storage/path_policy.hpp"
#include "nbt/storage/setup.hpp"
#include "support/check.hpp"

#include <errno.h>
#include <fcntl.h>
#include <limits>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace nbt::core;

static bool testStatus() {
    TEST_CHECK(Status().ok());
    TEST_CHECK(Status::error(0, "failure").code() != 0);
    Result<uint32_t> failure = Result<uint32_t>::failure(Status::error(EINVAL, "bad"));
    TEST_CHECK(!failure.get() && !failure.ok() && failure.status().code() == EINVAL);
    Result<uint32_t> value = Result<uint32_t>::success(UINT32_C(42));
    TEST_CHECK(value.get() && value.value() == 42);
    Result<uint32_t> moved = std::move(value);
    TEST_CHECK(moved.value() == 42 && !value.get());
    return true;
}

static bool testChecked() {
    uint8_t small;
    uint64_t wide;
    TEST_CHECK(checkedAdd<uint8_t>(0, 255, &small) && small == 255);
    TEST_CHECK(!checkedAdd<uint8_t>(1, 255, &small));
    TEST_CHECK(checkedSub<uint8_t>(255, 255, &small) && small == 0);
    TEST_CHECK(!checkedSub<uint8_t>(0, 1, &small));
    TEST_CHECK(checkedMul<uint64_t>(0, UINT64_MAX, &wide) && wide == 0);
    TEST_CHECK(checkedMul<uint64_t>(UINT64_MAX, 1, &wide) && wide == UINT64_MAX);
    TEST_CHECK(!checkedMul<uint64_t>(UINT64_MAX, 2, &wide));
    TEST_CHECK(checkedNarrow<uint8_t>(255, &small) && small == 255);
    TEST_CHECK(!checkedNarrow<uint8_t>(256, &small));
    TEST_CHECK(!checkedNarrow<uint64_t>(-1, &wide));
    TEST_CHECK(checkedNarrow<uint64_t>(INT64_MAX, &wide) &&
               wide == static_cast<uint64_t>(INT64_MAX));
    return true;
}

static int lockState(const char *path) {
    const pid_t child = fork();
    if (child == 0) {
        const int fd = open(path, O_RDONLY);
        struct flock range;
        memset(&range, 0, sizeof(range));
        range.l_type = F_WRLCK;
        range.l_whence = SEEK_SET;
        const int result = fd < 0 ? -1 : fcntl(fd, F_GETLK, &range);
        if (fd >= 0) {
            close(fd);
        }
        _exit(result ? 100 : range.l_type);
    }
    int status;
    return child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status)
               ? WEXITSTATUS(status)
               : -1;
}

static bool testUniqueFd() {
    char path[] = "/tmp/nbt-core-lock-XXXXXX";
    UniqueFd file(mkstemp(path));
    TEST_CHECK(file.valid());
    unlink(path);
    errno = EAGAIN;
    file.reset(file.get());
    TEST_CHECK(errno == EAGAIN && fcntl(file.get(), F_GETFD) >= 0);
    {
        UniqueFd moved = std::move(file);
        TEST_CHECK(!file.valid() && moved.valid());
        file = std::move(moved);
    }
    errno = EAGAIN;
    {
        UniqueFd bad(1000000);
    }
    TEST_CHECK(errno == EAGAIN);
    UniqueFd bad(1000000);
    TEST_CHECK(!bad.close().ok() && !bad.valid());

    UniqueFd lock(open(path, O_WRONLY | O_CREAT | O_EXCL, 0600));
    TEST_CHECK(lock.valid());
    struct flock range;
    memset(&range, 0, sizeof(range));
    range.l_type = F_WRLCK;
    range.l_whence = SEEK_SET;
    TEST_CHECK(fcntl(lock.get(), F_SETLK, &range) == 0);
    {
        UniqueFd moved = std::move(lock);
        TEST_CHECK(lockState(path) == F_WRLCK);
        lock = std::move(moved);
    }
    TEST_CHECK(lockState(path) == F_WRLCK);
    TEST_CHECK(lock.close().ok());
    TEST_CHECK(lockState(path) == F_UNLCK);
    unlink(path);
    return true;
}

static bool testSecure() {
    typename std::aligned_storage<sizeof(SecureBytes<32>), alignof(SecureBytes<32>)>::type storage;
    SecureBytes<32> *bytes = new (&storage) SecureBytes<32>();
    memset(bytes->data(), 0xa5, bytes->size());
    bytes->~SecureBytes();
    const unsigned char *raw = reinterpret_cast<const unsigned char *>(&storage);
    for (size_t i = 0; i < 32; ++i) {
        TEST_CHECK(raw[i] == 0);
    }
    SecureString<16> text;
    TEST_CHECK(text.set("secret") && text.size() == 6);
    TEST_CHECK(!text.set("0123456789abcdef0"));
    text.wipe();
    for (size_t i = 0; i <= text.capacity(); ++i) {
        TEST_CHECK(text.data()[i] == 0);
    }
    return true;
}

static bool testBytes() {
    uint8_t bytes[16] = {0};
    ByteWriter writer{Span<uint8_t>(bytes)};
    TEST_CHECK(writer.writeU16(0x1234) && writer.writeU32(0x89abcdefu) &&
               writer.writeU64(0x0123456789abcdefull));
    TEST_CHECK(writer.position() == 14 && !writer.writeU32(1));
    ByteReader reader{Span<const uint8_t>(bytes)};
    uint16_t shortValue;
    uint32_t intValue;
    uint64_t wideValue;
    TEST_CHECK(reader.readU16(&shortValue) && shortValue == 0x1234);
    TEST_CHECK(reader.readU32(&intValue) && intValue == 0x89abcdefu);
    TEST_CHECK(reader.readU64(&wideValue) && wideValue == 0x0123456789abcdefull);
    TEST_CHECK(reader.remaining() == 2 && !reader.readU32(&intValue));
    Span<const uint8_t> missing;
    TEST_CHECK(!reader.readBytes(&missing, 3));
    const Span<const uint8_t> empty;
    TEST_CHECK(empty.empty());
    return true;
}

static bool testScopeExit() {
    int calls = 0;
    {
        auto guard = onScopeExit([&]() { ++calls; });
        (void)guard;
    }
    TEST_CHECK(calls == 1);
    {
        auto guard = onScopeExit([&]() { ++calls; });
        guard.dismiss();
    }
    TEST_CHECK(calls == 1);
    StaticMutex mutex = NBT_STATIC_MUTEX_INITIALIZER;
    {
        LockGuard guard(mutex);
    }
    return true;
}

static bool testText() {
    TEST_CHECK(tokenValid(StringRef("app-1_x"), 16, 0));
    TEST_CHECK(!tokenValid(StringRef("App"), 16, 0) &&
               tokenValid(StringRef("App"), 16, kTokenUpper));
    TEST_CHECK(!tokenValid(StringRef("a.b"), 16, 0) && tokenValid(StringRef("a.b"), 16, kTokenDot));
    TEST_CHECK(!tokenValid(StringRef("abcd"), 3, 0));
    TEST_CHECK(parseU64(StringRef("18446744073709551615"), false).value() == UINT64_MAX);
    TEST_CHECK(!parseU64(StringRef("18446744073709551616"), false).get());
    TEST_CHECK(!parseU64(StringRef("01"), false).get());
    TEST_CHECK(parseU64(StringRef("01"), true).value() == 1);
    TEST_CHECK(!parseU64(StringRef("-1"), true).get());
    uint8_t decoded[2];
    TEST_CHECK(hexDecode(StringRef("00ff"), Span<uint8_t>(decoded)).ok());
    TEST_CHECK(decoded[0] == 0 && decoded[1] == 255);
    TEST_CHECK(!hexDecode(StringRef("0g"), Span<uint8_t>(decoded, 1)).ok());
    char encoded[5];
    TEST_CHECK(hexEncode(Span<const uint8_t>(decoded), Span<char>(encoded)).ok());
    TEST_CHECK(strcmp(encoded, "00ff") == 0);
    FieldReader fields(StringRef("name=value\nempty=\n"));
    StringRef value;
    TEST_CHECK(fields.next(StringRef("name"), &value) && value == StringRef("value"));
    TEST_CHECK(!fields.next(StringRef("wrong"), &value));
    TEST_CHECK(fields.next(StringRef("empty"), &value) && value.empty());
    FieldReader malformed(StringRef("name=value"));
    TEST_CHECK(!malformed.next(StringRef("name"), &value));
    return true;
}

static bool testPathPolicy() {
    char path[NBT_PATH_MAX];
    const char tx[] = "000102030405060708090a0b0c0d0e0f";
    TEST_CHECK(
        nbt::storage::PathPolicy::relative(NBT_FS_ACTIVE, "app-1", NULL, Span<char>(path)).ok());
    TEST_CHECK(strcmp(path, "apps/app-1/bin/app") == 0);
    TEST_CHECK(
        nbt::storage::PathPolicy::relative(NBT_FS_CANDIDATE, "app-1", tx, Span<char>(path)).ok());
    TEST_CHECK(strcmp(path, "apps/app-1/bin/.candidate-000102030405060708090a0b0c0d0e0f") == 0);
    TEST_CHECK(
        !nbt::storage::PathPolicy::relative(NBT_FS_CANDIDATE, "app-1", "bad", Span<char>(path))
             .ok());
    TEST_CHECK(
        !nbt::storage::PathPolicy::relative(NBT_FS_ACTIVE, "../escape", NULL, Span<char>(path))
             .ok());
    TEST_CHECK(nbt::storage::PathPolicy::relative(NBT_FS_MANAGER_SLOT_GUI_NEXT, "manager", "1",
                                                  Span<char>(path))
                   .ok());
    TEST_CHECK(strcmp(path, "manager/slots/1/gui.next") == 0);
    TEST_CHECK(!nbt::storage::PathPolicy::relative(NBT_FS_MANAGER_SLOT_GUI_NEXT, "manager", "2",
                                                   Span<char>(path))
                    .ok());
    TEST_CHECK(
        !nbt::storage::PathPolicy::relative(NBT_FS_ACTIVE, "app", NULL, Span<char>(path, 4)).ok());
    return true;
}

static bool testFaultPoint() {
    const pid_t child = fork();
    if (child == 0) {
        setenv("NBT_TEST_CORE_FAULT", "step", 1);
        NBT_FAULT_POINT("NBT_TEST_CORE_FAULT", "step");
        _exit(0);
    }
    int status;
    TEST_CHECK(child > 0 && waitpid(child, &status, 0) == child);
    TEST_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 91);
    return true;
}

static bool testStringRefAndStatusLifetime() {
    const char one[1] = {'a'};
    const StringRef oneRef(one, sizeof(one));
    char small[2] = {'x', 'x'};
    TEST_CHECK(copyTerminated(oneRef, Span<char>(small)).ok());
    TEST_CHECK(strcmp(small, "a") == 0);
    TEST_CHECK(!copyTerminated(oneRef, Span<char>(small, 1)).ok());
    const char embeddedNul[3] = {'a', '\0', 'b'};
    TEST_CHECK(
        !copyTerminated(StringRef(embeddedNul, sizeof(embeddedNul)), Span<char>(small)).ok());

    TEST_CHECK(!nbt::sdk::Setting::parse(oneRef).get());
    const char rawDecl[] = {'m', 'o', 'd', 'e', '|', 'M', 'o', 'd',
                            'e', '|', 'a', '|', 'a', ',', 'b'};
    const StringRef declRef(rawDecl, sizeof(rawDecl));
    Result<nbt::sdk::Setting> parsed = nbt::sdk::Setting::parse(declRef);
    TEST_CHECK(parsed.get());
    TEST_CHECK(parsed.value().allows(oneRef));
    const char badChoice[1] = {'z'};
    TEST_CHECK(!parsed.value().allows(StringRef(badChoice, sizeof(badChoice))));

    nbt::sdk::SettingsList list;
    TEST_CHECK(!list.add(oneRef).ok());
    TEST_CHECK(list.add(declRef).ok());
    const char rawKey[] = {'m', 'o', 'd', 'e'};
    TEST_CHECK(list.find(StringRef(rawKey, sizeof(rawKey))) == 0);
    TEST_CHECK(list.find(oneRef) == -1);

    nbt::model::Provider provider;
    TEST_CHECK(!provider.add(oneRef).ok());
    const char rawProvider[] = {'1', '.', '0', '|', 's', 'c', 'r', 'e', 'e', 'n'};
    TEST_CHECK(provider.add(StringRef(rawProvider, sizeof(rawProvider))).ok());

    char statusOut[NBT_BRIDGE_STATUS_LINE_MAX + 1];
    Result<size_t> formatted = nbt::model::formatStatus(7, oneRef, Span<char>(statusOut));
    TEST_CHECK(formatted.get() && formatted.value() == 4);
    TEST_CHECK(memcmp(statusOut, "7 a\n", 4) == 0);

    nbt::storage::ManagedRoot root;
    Status rootInit = root.initialize("/nonexistent/nbt-core-test-missing");
    TEST_CHECK(!rootInit.ok() && strstr(rootInit.message(), "managed root") != NULL);
    Status setupInit = nbt::storage::SetupLayout::createRoot(&root, "relative-bad-path", 1024);
    TEST_CHECK(!setupInit.ok() && strlen(setupInit.message()) > 0);

    char reqText[] = "bridge=9.0\nbridge-uses=screen\nbridge-optional=\n";
    char *cursor = reqText;
    Result<nbt::model::BridgeRequirement> req = nbt::model::BridgeRequirement::parse(&cursor, "");
    TEST_CHECK(req.get());
    char bridgeErr[128] = {0};
    Result<nbt_bridge_version> check = provider.check(req.value(), NULL, Span<char>(bridgeErr));
    TEST_CHECK(!check.get() && strlen(check.status().message()) > 0 && strlen(bridgeErr) > 0);
    char envPath[] = "PATH=/old";
    char envLib[] = "LD_LIBRARY_PATH=/opt/sys/lib";
    char envPreload[] = "LD_PRELOAD=/opt/sys/lib/libEGL_X.gpp.so";
    char envTz[] = "TZ=UTC0";
    char *fakeEnv[] = {envPath, envLib, envPreload, envTz, NULL};
    char *withPreload[nbt::process::kHmiEnvironmentMax];
    char *withoutPreload[nbt::process::kHmiEnvironmentMax];
    char preloadBuf[512];
    TEST_CHECK(nbt::process::hmiEnvironmentFrom(fakeEnv, withPreload, preloadBuf,
                                                sizeof(preloadBuf), true));
    TEST_CHECK(nbt::process::hmiEnvironmentFrom(fakeEnv, withoutPreload, preloadBuf,
                                                sizeof(preloadBuf), false));
    TEST_CHECK(strncmp(withPreload[0], "PATH=/bin:", 10) == 0);
    TEST_CHECK(withPreload[1] == envLib && withPreload[2] == envPreload &&
               withPreload[3] == envTz && !withPreload[4]);
    TEST_CHECK(withoutPreload[0] == withPreload[0] && withoutPreload[1] == envLib &&
               withoutPreload[2] == envTz && !withoutPreload[3]);

    /* Capture shim stripping: only exact path stripped, order preserved, overflow fails. */
    char envPreloadMixed[] =
        "LD_PRELOAD=libEGL_X.gpp.so:/opt/hmi/ID5/data/ro/NbtCapture/libnbt-capture.so:"
        "libGLESv2_X_FORWARD.gpp.so:/opt/hmi/ID5/data/ro/NbtCapture/libnbt-capture.so.bak";
    char *mixedEnv[] = {envLib, envPreloadMixed, envTz, NULL};
    char *stripped[nbt::process::kHmiEnvironmentMax];
    TEST_CHECK(
        nbt::process::hmiEnvironmentFrom(mixedEnv, stripped, preloadBuf, sizeof(preloadBuf), true));
    TEST_CHECK(strcmp(stripped[2],
                      "LD_PRELOAD=libEGL_X.gpp.so:libGLESv2_X_FORWARD.gpp.so:/opt/hmi/ID5/data/ro/"
                      "NbtCapture/libnbt-capture.so.bak") == 0);

    char envPreloadNew[] = "LD_PRELOAD=libEGL_X.gpp.so:/var/opt/nbt-manager/state/manager/"
                           "libnbt-capture.so";
    char *newEnv[] = {envLib, envPreloadNew, envTz, NULL};
    TEST_CHECK(
        nbt::process::hmiEnvironmentFrom(newEnv, stripped, preloadBuf, sizeof(preloadBuf), true));
    TEST_CHECK(strcmp(stripped[2], "LD_PRELOAD=libEGL_X.gpp.so") == 0);

    char envPreloadOnly[] = "LD_PRELOAD=/opt/hmi/ID5/data/ro/NbtCapture/libnbt-capture.so";
    char *onlyEnv[] = {envLib, envPreloadOnly, envTz, NULL};
    TEST_CHECK(
        nbt::process::hmiEnvironmentFrom(onlyEnv, stripped, preloadBuf, sizeof(preloadBuf), true));
    TEST_CHECK(stripped[1] == envLib && stripped[2] == envTz && !stripped[3]);

    char tinyPreload[24];
    TEST_CHECK(!nbt::process::hmiEnvironmentFrom(mixedEnv, stripped, tinyPreload,
                                                 sizeof(tinyPreload), true));
    char *hugeEnv[nbt::process::kHmiEnvironmentMax + 2];
    for (size_t i = 0; i < nbt::process::kHmiEnvironmentMax; ++i) {
        hugeEnv[i] = envTz;
    }
    hugeEnv[nbt::process::kHmiEnvironmentMax] = NULL;
    TEST_CHECK(
        !nbt::process::hmiEnvironmentFrom(hugeEnv, stripped, preloadBuf, sizeof(preloadBuf), true));
    return true;
}

int main() {
    TEST_RUN(testStatus);
    TEST_RUN(testChecked);
    TEST_RUN(testUniqueFd);
    TEST_RUN(testSecure);
    TEST_RUN(testBytes);
    TEST_RUN(testScopeExit);
    TEST_RUN(testText);
    TEST_RUN(testPathPolicy);
    TEST_RUN(testFaultPoint);
    TEST_RUN(testStringRefAndStatusLifetime);
    puts("core-test: OK");
    return 0;
}

#include "nbt/storage/phone_store.hpp"
#include "nbt/worker/ipc_dispatcher.hpp"
#include <stdio.h>
#include <string.h>
namespace nbt {
namespace worker {
core::Status IpcDispatcher::handlePhone(const char *command, uint64_t now) {
    char output[1024] = {};
    storage::PhoneStore phones(root_);
    bool ok = false;
    if (!strcmp(command, "PHONE LIST")) {
        storage::Phone list[4];
        const int count = phones.list(list);
        if (count >= 0) {
            int n = snprintf(output, sizeof(output), "PHONES\t%d", count);
            size_t used = static_cast<size_t>(n);
            for (int i = 0; i < count && used < sizeof(output); ++i) {
                n = snprintf(output + used, sizeof(output) - used, "\t%s|%s", list[i].id,
                             list[i].name);
                if (n < 0 || static_cast<size_t>(n) >= sizeof(output) - used) {
                    return control_.respond("ERR phones\n");
                }
                used += static_cast<size_t>(n);
            }
            if (used + 2 <= sizeof(output)) {
                output[used++] = '\n';
                output[used] = 0;
                ok = true;
            }
        }
    } else if (!strncmp(command, "PHONE FORGET ", 13)) {
        ok = phones.remove(command + 13);
        if (ok) {
            snprintf(output, sizeof(output), "OK\n");
        }
    } else {
        ok = web_.api().local(command, now, core::Span<char>(output, sizeof(output)));
    }
    return control_.respond(ok ? output : "ERR phones\n");
}
} // namespace worker
} // namespace nbt

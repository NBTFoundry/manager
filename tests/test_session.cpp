#include "nbt/worker/session.hpp"
#include <stdio.h>
int main() {
    nbt::worker::Session session;
    if (!session.start(100).ok() || !session.active(100) || session.uploaded()) {
        return 1;
    }
    session.setUploaded(true);
    if (session.active(100 + nbt::worker::Session::kTtlMs) || session.started() ||
        session.uploaded()) {
        return 1;
    }
    if (session.start(UINT64_MAX).ok() || session.started()) {
        return 1;
    }
    if (!session.start(200).ok()) {
        return 1;
    }
    nbt::worker::Session moved(static_cast<nbt::worker::Session &&>(session));
    if (session.started() || !moved.active(200)) {
        return 1;
    }
    moved.end();
    return moved.started() ? 1 : 0;
}

// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/phone_actions.hpp"
#include "nbt/app_remove.h"
#include "nbt/crypto/ec_public_key.hpp"
#include "nbt/util.h"
#include "nbt/worker/phone_auth.hpp"
#include "nbt/worker/worker.hpp"
#include <stdio.h>
#include <string.h>
#include <time.h>
namespace nbt {
namespace worker {
PhoneActions::PhoneActions(Worker &worker, storage::PhoneStore &phones,
                           storage::DeveloperStore &developers)
    : worker_(worker), phones_(phones), developers_(developers), keyCandidate_(), keyRemove_(),
      pairDeadline_(0), pendingDeadline_(0), resultDeadline_(0), candidate_(), resultMessage_(),
      owner_(), pendingApp_(), result_(), code_(), actionId_() {
}
void PhoneActions::clearPending() {
    pendingDeadline_ = 0;
    pendingApp_[0] = 0;
    keyCandidate_ = storage::Developer();
    keyRemove_[0] = 0;
}
void PhoneActions::expire(uint64_t now) {
    if (pairDeadline_ && now >= pairDeadline_) {
        pairDeadline_ = 0;
    }
    if (pendingDeadline_ && now >= pendingDeadline_) {
        clearPending();
        strcpy(result_, "expired");
        strcpy(resultMessage_, "Request expired; try again from the phone");
    }
    if (resultDeadline_ && now >= resultDeadline_ && worker_.state().state != State::Installing) {
        owner_[0] = result_[0] = 0;
        candidate_ = storage::Phone();
        resultDeadline_ = 0;
    }
}
bool PhoneActions::parsePair(const char *body, storage::Phone &phone) {
    char copy[256];
    if (!body || strlen(body) >= sizeof(copy)) {
        return false;
    }
    strcpy(copy, body);
    if (strncmp(copy, "NBTPAIR1\n", 9)) {
        return false;
    }
    char *cursor = copy + 9;
    char *name = nbt_field(&cursor, "name"), *key = nbt_field(&cursor, "key");
    if (!storage::PhoneStore::nameValid(name) || !key || *cursor ||
        nbt_hex_decode(key, phone.key, 65) || !crypto::EcPublicKey::valid(phone.key) ||
        !crypto::EcPublicKey::id(phone.key, phone.id)) {
        return false;
    }
    strcpy(phone.name, name);
    return true;
}
bool PhoneActions::candidate(const char *id, storage::Phone &phone) const {
    if (!strcmp(result_, "paired") || !owner_[0] || strcmp(id, owner_) ||
        strcmp(id, candidate_.id)) {
        return false;
    }
    phone = candidate_;
    return true;
}
int PhoneActions::pair(const char *key, const char *nonce, const char *body, uint64_t now,
                       core::Span<char> out) {
    storage::Phone phones[4], proposed;
    const int count = phones_.list(phones);
    if (!canPair(now) || count < 0 || count >= 4 || !parsePair(body, proposed) ||
        !phonePairingCode(nonce, proposed.key, code_) || nbt_fs_random_tx(actionId_)) {
        return 409;
    }
    for (int i = 0; i < count; ++i) {
        if (!strcmp(phones[i].id, proposed.id)) {
            return 409;
        }
    }
    const time_t epoch = time(NULL);
    if (epoch <= 0) {
        return 503;
    }
    candidate_ = proposed;
    candidate_.paired = static_cast<uint64_t>(epoch);
    strcpy(owner_, key);
    strcpy(result_, "pairing");
    strcpy(resultMessage_, "Compare the digits and confirm on the car");
    pendingApp_[0] = 0;
    keyCandidate_ = storage::Developer();
    keyRemove_[0] = 0;
    pendingDeadline_ = pairDeadline_;
    resultDeadline_ = now + 300000;
    snprintf(out.data(), out.size(), "PAIR\tpending\t%s\n", code_);
    return 202;
}
void PhoneActions::uploaded(const char *key, uint64_t now) {
    strcpy(owner_, key);
    result_[0] = 0;
    candidate_ = storage::Phone();
    resultDeadline_ = now + 300000;
}
int PhoneActions::remove(const char *key, const char *body, uint64_t now, core::Span<char> out) {
    char app[49];
    const size_t size = strlen(body);
    if (size < 2 || size > 49 || body[size - 1] != '\n') {
        return 400;
    }
    memcpy(app, body, size - 1);
    app[size - 1] = 0;
    if (!nbt_token_valid(app, 48, 0) || !strcmp(app, "manager") ||
        nbt_fs_exists(worker_.root().legacy(), NBT_FS_PROFILE, app, NULL) != 1 ||
        nbt_fs_random_tx(actionId_)) {
        return 400;
    }
    if (worker_.state().session.active(now)) {
        worker_.cancel(now);
    }
    strcpy(owner_, key);
    strcpy(pendingApp_, app);
    candidate_ = storage::Phone();
    keyCandidate_ = storage::Developer();
    keyRemove_[0] = 0;
    strcpy(result_, "removal");
    strcpy(resultMessage_, "Confirm uninstall on the car");
    pendingDeadline_ = now + 60000;
    resultDeadline_ = now + 300000;
    snprintf(out.data(), out.size(), "STATUS\twaiting\tConfirm uninstall on the car\n");
    return 202;
}
bool PhoneActions::begin(const char *key, const char *result, const char *message, uint64_t now) {
    if (nbt_fs_random_tx(actionId_)) {
        return false;
    }
    if (worker_.state().session.active(now)) {
        worker_.cancel(now);
    }
    strcpy(owner_, key);
    candidate_ = storage::Phone();
    strcpy(result_, result);
    snprintf(resultMessage_, sizeof(resultMessage_), "%s", message);
    pendingDeadline_ = now + 60000;
    resultDeadline_ = now + 300000;
    return true;
}
bool PhoneActions::confirm(const char *id, bool yes, uint64_t now) {
    if (!pendingDeadline_ || now >= pendingDeadline_ || strcmp(actionId_, id)) {
        return false;
    }
    if (!yes) {
        strcpy(result_, "cancelled");
        strcpy(resultMessage_, "Rejected on the car");
    } else if (keyCandidate_.id[0]) {
        const bool added = developers_.add(keyCandidate_);
        strcpy(result_, added ? "key-added" : "failed");
        strcpy(resultMessage_, added ? "Developer key added" : "Cannot save the key on the car");
    } else if (keyRemove_[0]) {
        const bool removed = developers_.remove(keyRemove_);
        strcpy(result_, removed ? "key-removed" : "failed");
        strcpy(resultMessage_, removed ? "Developer key removed" : "Cannot remove the key");
    } else if (candidate_.id[0]) {
        if (!phones_.add(candidate_)) {
            strcpy(result_, "failed");
            strcpy(resultMessage_, "Cannot save phone on the car");
            clearPending();
            return false;
        }
        strcpy(result_, "paired");
        strcpy(resultMessage_, "Phone paired");
        pairDeadline_ = 0;
    } else {
        const core::Result<int> removed = worker_.remove(pendingApp_, now);
        strcpy(result_, removed.ok() && (removed.value() == NBT_APP_REMOVED ||
                                         removed.value() == NBT_APP_REMOVE_MISSING)
                            ? "removed"
                            : "failed");
        const char *message = !strcmp(result_, "removed") ? "Application uninstalled"
                              : removed.ok() && removed.value() == NBT_APP_REMOVE_RUNNING
                                  ? "Stop the application on the car before uninstalling"
                              : removed.ok() && removed.value() == NBT_APP_REMOVE_BUSY
                                  ? "Car busy; try again"
                                  : "Uninstall failed";
        snprintf(resultMessage_, sizeof(resultMessage_), "%s", message);
    }
    clearPending();
    return true;
}
int PhoneActions::status(const char *key, uint64_t now, const char *bssid, const char *host,
                         core::Span<char> out) {
    expire(now);
    if (strcmp(key, owner_)) {
        return 403;
    }
    const WorkerState &state = worker_.state();
    const char *code = result_[0] ? result_ : "waiting";
    const char *message = result_[0] ? resultMessage_ : state.message;
    if (!result_[0]) {
        if (state.state == State::Verified) {
            code = "verified";
        } else if (state.state == State::Installing) {
            code = "installing";
            resultDeadline_ = now + 300000;
        } else if (state.state == State::Installed) {
            code = "installed";
        } else if (state.state == State::Failed) {
            code = "failed";
        } else if (state.ended == Ended::Cancelled) {
            code = "cancelled";
        } else if (state.ended == Ended::Expired) {
            code = "expired";
        }
    }
    if (!strcmp(code, "pairing") || !strcmp(code, "removal") || !strcmp(code, "key") ||
        !strcmp(code, "unkey")) {
        code = "waiting";
    }
    const int n = !strcmp(code, "paired")
                      ? snprintf(out.data(), out.size(),
                                 "STATUS\tpaired\tPhone paired\nCAR\t%s\t%s\n", bssid, host)
                      : snprintf(out.data(), out.size(), "STATUS\t%s\t%s\n", code,
                                 message[0] ? message : "Waiting");
    return n > 0 && static_cast<size_t>(n) < out.size() ? 200 : 500;
}
bool PhoneActions::local(const char *command, uint64_t now, core::Span<char> out) {
    expire(now);
    if (!strcmp(command, "PHONE OPEN")) {
        if (pendingDeadline_ || worker_.state().state == State::Verified ||
            worker_.state().state == State::Installing) {
            return false;
        }
        pairDeadline_ = now + 60000;
        snprintf(out.data(), out.size(), "OK\n");
        return true;
    }
    if (!strcmp(command, "PHONE CLOSE")) {
        pairDeadline_ = 0;
        if (pendingDeadline_) {
            clearPending();
            strcpy(result_, "cancelled");
            strcpy(resultMessage_, "Rejected on the car");
        }
        snprintf(out.data(), out.size(), "OK\n");
        return true;
    }
    if (!strncmp(command, "PHONE YES ", 10) || !strncmp(command, "PHONE NO ", 9)) {
        const bool yes = command[6] == 'Y';
        if (!confirm(command + (yes ? 10 : 9), yes, now)) {
            return false;
        }
        snprintf(out.data(), out.size(), "OK %s\n", result_);
        return strcmp(result_, "failed") != 0;
    }
    if (!strcmp(command, "PHONE STATUS")) {
        storage::Phone phone;
        if (!pendingDeadline_) {
            snprintf(out.data(), out.size(), "PHONE\tnone\n");
        } else if (candidate_.id[0]) {
            snprintf(out.data(), out.size(), "PHONE\tpair\t%s\t%s\t%s\n", actionId_,
                     candidate_.name, code_);
        } else if ((keyCandidate_.id[0] || keyRemove_[0]) && phones_.find(owner_, phone)) {
            storage::Developer developer = keyCandidate_;
            char print[20];
            if (keyRemove_[0] && !developers_.find(keyRemove_, developer)) {
                return false;
            }
            storage::DeveloperStore::fingerprint(developer.id, print);
            snprintf(out.data(), out.size(), "PHONE\t%s\t%s\t%s\t%s\t%s\n",
                     keyRemove_[0] ? "unkey" : "key", actionId_, phone.name, developer.name, print);
        } else if (phones_.find(owner_, phone)) {
            snprintf(out.data(), out.size(), "PHONE\tremove\t%s\t%s\t%s\n", actionId_, phone.name,
                     pendingApp_);
        } else {
            return false;
        }
        return true;
    }
    return false;
}
} // namespace worker
} // namespace nbt

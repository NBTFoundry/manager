// SPDX-License-Identifier: MPL-2.0
#include <zlib.h>

#include "nbt/bridge.h"
#include "nbt/bridge_hold.h"
#include "nbt/capture/classifier.h"
#include "nbt/capture/control.h"
#include "nbt/ccs.h"
#include "nbt/cds_policy.h"
#include "nbt/controller_config.h"
#include "nbt/core/span.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/crypto/ec_public_key.hpp"
#include "nbt/files_policy.h"
#include "nbt/gamepad/input.hpp"
#include "nbt/gamepad/reports.hpp"
#include "nbt/gui/car_language.hpp"
#include "nbt/gui/phone_wake.hpp"
#include "nbt/gui/shortcut_record.hpp"
#include "nbt/gui/text.hpp"
#include "nbt/gui/worker_text.hpp"
#include "nbt/lifecycle/transactions.hpp"
#include "nbt/model/installed_record.hpp"
#include "nbt/model/language.hpp"
#include "nbt/most.h"
#include "nbt/most_policy.h"
#include "nbt/net_policy.h"
#include "nbt/package/package_verifier.hpp"
#include "nbt/package/release_verifier.hpp"
#include "nbt/settings.h"
#include "nbt/storage/starter_config.hpp"
#include "nbt/storage/zip_archive.hpp"
#include "nbt/worker/control_protocol.hpp"
#include "nbt/worker/http.hpp"
#include "nbt/worker/phone_actions.hpp"
#include "nbt/worker/usb_files.hpp"
#include "nbt/worker/wifi_direct_most.hpp"

#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

const size_t kInputMax = 16384;

enum Target {
    GamepadWire,
    GamepadGip,
    GamepadSony,
    Http,
    PhoneSignature,
    PhonePair,
    PhoneContext,
    Control,
    PackageHeader,
    PackageManifest,
    ReleaseManifest,
    SettingsSchema,
    SettingsValues,
    ControllerConfig,
    InstalledRecord,
    Journal,
    MostCodec,
    MostJoiner,
    Ccs,
    MostPolicy,
    FilesPolicy,
    BridgeRequirement,
    BridgeStatus,
    WfdMost,
    CaptureControl,
    CaptureStream,
    StarterConfigTarget,
    LanguageTarget,
    CdsPolicy,
    NetPolicy,
    BridgeHold,
    ZipArchiveTarget
};

struct TargetName {
    const char *name;
    Target target;
};

const TargetName kTargets[] = {{"gamepad-wire", GamepadWire},
                               {"gamepad-gip", GamepadGip},
                               {"gamepad-sony", GamepadSony},
                               {"http", Http},
                               {"phone-signature", PhoneSignature},
                               {"phone-pair", PhonePair},
                               {"phone-context", PhoneContext},
                               {"control", Control},
                               {"package-header", PackageHeader},
                               {"package-manifest", PackageManifest},
                               {"release-manifest", ReleaseManifest},
                               {"settings-schema", SettingsSchema},
                               {"settings-values", SettingsValues},
                               {"controller-config", ControllerConfig},
                               {"installed-record", InstalledRecord},
                               {"journal", Journal},
                               {"most-codec", MostCodec},
                               {"most-joiner", MostJoiner},
                               {"ccs", Ccs},
                               {"most-policy", MostPolicy},
                               {"files-policy", FilesPolicy},
                               {"bridge-requirement", BridgeRequirement},
                               {"bridge-status", BridgeStatus},
                               {"wfd-most", WfdMost},
                               {"capture-control", CaptureControl},
                               {"capture-stream", CaptureStream},
                               {"starter-config", StarterConfigTarget},
                               {"language", LanguageTarget},
                               {"cds-policy", CdsPolicy},
                               {"net-policy", NetPolicy},
                               {"bridge-hold", BridgeHold},
                               {"zip-archive", ZipArchiveTarget}};

class Mutator {
public:
    explicit Mutator(uint32_t seed) : state_(seed ? seed : 1) {
    }

    uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    size_t below(size_t limit) {
        return limit ? static_cast<size_t>(next() % limit) : 0;
    }

    void mutate(std::vector<uint8_t> *bytes) {
        const unsigned operations = 1 + static_cast<unsigned>(below(6));
        for (unsigned i = 0; i < operations; ++i) {
            switch (below(6)) {
            case 0:
                if (!bytes->empty()) {
                    (*bytes)[below(bytes->size())] ^= static_cast<uint8_t>(1u << below(8));
                }
                break;
            case 1:
                if (!bytes->empty()) {
                    (*bytes)[below(bytes->size())] = static_cast<uint8_t>(next());
                }
                break;
            case 2:
                if (!bytes->empty()) {
                    const size_t at = below(bytes->size());
                    const size_t count = std::min(bytes->size() - at, 1 + below(32));
                    bytes->erase(
                        bytes->begin() + static_cast<std::vector<uint8_t>::difference_type>(at),
                        bytes->begin() +
                            static_cast<std::vector<uint8_t>::difference_type>(at + count));
                }
                break;
            case 3: {
                const size_t count = std::min(kInputMax - bytes->size(), 1 + below(32));
                const size_t at = below(bytes->size() + 1);
                bytes->insert(bytes->begin() +
                                  static_cast<std::vector<uint8_t>::difference_type>(at),
                              count, 0);
                for (size_t j = 0; j < count; ++j) {
                    (*bytes)[at + j] = static_cast<uint8_t>(next());
                }
                break;
            }
            case 4:
                if (bytes->size() < kInputMax) {
                    bytes->push_back(static_cast<uint8_t>(next()));
                }
                break;
            default:
                if (below(20) == 0) {
                    bytes->clear();
                }
                break;
            }
        }
    }

private:
    uint32_t state_;
};

class Commands : public nbt::worker::ControlHandler {
public:
    nbt::core::Status command(char *line, uint64_t nowMs) override {
        (void)line;
        (void)nowMs;
        return nbt::core::Status();
    }
};

/* A one-entry zip archive, "Game/game.cue" holding "FILE x BINARY", stored
 * (method 0) or with a deflate stream of stored blocks (method 8). */
std::vector<uint8_t> zipSeed(unsigned method) {
    static const char kName[] = "Game/game.cue";
    static const char kData[] = "FILE x BINARY";
    const uint32_t nameLength = sizeof(kName) - 1, size = sizeof(kData) - 1;
    std::vector<uint8_t> data(kData, kData + size);
    if (method == 8) { // one final stored deflate block
        const uint8_t head[] = {1, static_cast<uint8_t>(size), 0, static_cast<uint8_t>(~size),
                                0xff};
        data.insert(data.begin(), head, head + sizeof(head));
    }
    const uint32_t crc = static_cast<uint32_t>(
        crc32(0L, reinterpret_cast<const Bytef *>(kData), static_cast<uInt>(size)));
    std::vector<uint8_t> out;
    auto le = [&out](uint32_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) {
            out.push_back(static_cast<uint8_t>(value >> (8 * i)));
        }
    };
    le(0x04034b50, 4);
    le(20, 2);
    le(0, 2);
    le(method, 2);
    le(0, 4);
    le(crc, 4);
    le(static_cast<uint32_t>(data.size()), 4);
    le(size, 4);
    le(nameLength, 2);
    le(0, 2);
    out.insert(out.end(), kName, kName + nameLength);
    out.insert(out.end(), data.begin(), data.end());
    const uint32_t central = static_cast<uint32_t>(out.size());
    le(0x02014b50, 4);
    le(20, 2);
    le(20, 2);
    le(0, 2);
    le(method, 2);
    le(0, 4);
    le(crc, 4);
    le(static_cast<uint32_t>(data.size()), 4);
    le(size, 4);
    le(nameLength, 2);
    le(0, 2);
    le(0, 2);
    le(0, 2);
    le(0, 2);
    le(0, 4);
    le(0, 4);
    out.insert(out.end(), kName, kName + nameLength);
    const uint32_t directory = static_cast<uint32_t>(out.size()) - central;
    le(0x06054b50, 4);
    le(0, 2);
    le(0, 2);
    le(1, 2);
    le(1, 2);
    le(directory, 4);
    le(central, 4);
    le(0, 2);
    return out;
}

void addSeed(std::vector<std::vector<uint8_t>> *seeds, const char *text) {
    seeds->push_back(std::vector<uint8_t>(reinterpret_cast<const uint8_t *>(text),
                                          reinterpret_cast<const uint8_t *>(text) + strlen(text)));
}

void addSeed(std::vector<std::vector<uint8_t>> *seeds, const std::string &text) {
    seeds->push_back(std::vector<uint8_t>(text.begin(), text.end()));
}

void appendBe32(std::vector<uint8_t> *bytes, uint32_t value) {
    for (unsigned shift = 32; shift; shift -= 8) {
        bytes->push_back(static_cast<uint8_t>(value >> (shift - 8)));
    }
}

void appendBe64(std::vector<uint8_t> *bytes, uint64_t value) {
    appendBe32(bytes, static_cast<uint32_t>(value >> 32));
    appendBe32(bytes, static_cast<uint32_t>(value));
}

std::string packageManifest() {
    return "app=test\npackage=00112233445566778899aabbccddeeff\nversion=v1\nsequence=1\n"
           "platform=qnx6.5-armv7le\ncompat=demo-1\nsize=52\nsha256=" +
           std::string(64, '0') + "\n";
}

std::string releaseManifest() {
    static const char *const names[] = {
        "launcher",        "gui",        "worker",      "plugin_lib",
        "plugin_manifest", "plugin_xml", "plugin_text", "plugin_images"};
    std::string text = "version=v1\nsequence=1\nplatform=qnx6.5-armv7le\ncompat=demo-1\n";
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        text += names[i];
        text += "_size=52\n";
        text += names[i];
        text += "_sha256=" + std::string(64, '0') + "\n";
    }
    return text;
}

std::vector<std::vector<uint8_t>> seeds(Target target) {
    std::vector<std::vector<uint8_t>> out;
    switch (target) {
    case Http:
        addSeed(&out, "GET /v1/nonce HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        addSeed(&out,
                "POST /v1/upload HTTP/1.1\r\nHost: x\r\nOrigin: http://x\r\nContent-Length: 0\r\n"
                "Content-Type: application/octet-stream\r\n\r\n");
        addSeed(&out, "POST /v1/logs/app HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n"
                      "Content-Type: text/plain\r\nX-NBT-Key: "
                      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n\r\n");
        break;
    case PhoneSignature:
        out.push_back(std::vector<uint8_t>(105, 0));
        break;
    case PhonePair:
        addSeed(&out, "NBTPAIR1\nname=Phone\nkey=00\n");
        break;
    case PhoneContext:
        addSeed(&out, "{\"speedActual\":0}");
        break;
    case Control:
        addSeed(&out, "HELLO 1\nSTATUS\nLIST\nQUIT\n");
        addSeed(&out, "garbage without a newline");
        break;
    case PackageHeader: {
        const std::string manifest = packageManifest();
        std::vector<uint8_t> bytes;
        addSeed(&out, manifest);
        const std::vector<uint8_t> text = out.front();
        out.clear();
        bytes.insert(bytes.end(), reinterpret_cast<const uint8_t *>("NBTMGR1\n"),
                     reinterpret_cast<const uint8_t *>("NBTMGR1\n") + 8);
        appendBe32(&bytes, static_cast<uint32_t>(text.size()));
        appendBe32(&bytes, NBT_SIGNATURE_SIZE);
        appendBe64(&bytes, 52);
        bytes.insert(bytes.end(), text.begin(), text.end());
        bytes.resize(bytes.size() + NBT_SIGNATURE_SIZE, 0);
        const size_t payload = bytes.size();
        bytes.resize(payload + 52, 0);
        bytes[payload] = 0x7f;
        bytes[payload + 1] = 'E';
        bytes[payload + 2] = 'L';
        bytes[payload + 3] = 'F';
        bytes[payload + 4] = 1;
        bytes[payload + 5] = 1;
        bytes[payload + 6] = 1;
        bytes[payload + 16] = 2;
        bytes[payload + 18] = 40;
        out.push_back(bytes);
        break;
    }
    case PackageManifest:
        addSeed(&out, packageManifest());
        addSeed(&out, "app=test\n");
        break;
    case ReleaseManifest:
        addSeed(&out, releaseManifest());
        break;
    case SettingsSchema:
        addSeed(&out, "NBTSETTINGS1\nsetting=mode|Mode|on|on,off\n");
        break;
    case SettingsValues:
        addSeed(&out, "NBTVALUES1\nmode=off\n");
        break;
    case ControllerConfig:
        addSeed(
            &out,
            "NBTCTL1\nknob_instance=03\nccs_instance=01\ninstances=01,03\ninvert=0\n"
            "knob_menu_slot=0\nknob_back_slot=1\nccs_menu_slot=0\nccs_back_slot=1\nrelease=0\n");
        break;
    case InstalledRecord:
        addSeed(&out, "NBTSTATE1\nsequence=00000000000000000001\nhighwater=00000000000000000001\n"
                      "version=v1\nsha256="
                      "0000000000000000000000000000000000000000000000000000000000000000\n");
        break;
    case Journal:
        addSeed(&out,
                "NBTJ1\ntx=00112233445566778899aabbccddeeff\nphase=PREPARED\n"
                "old_sequence=00000000000000000001\nold_highwater=00000000000000000001\nold_"
                "version=v1\n"
                "old_sha256=0000000000000000000000000000000000000000000000000000000000000000\n"
                "new_sequence=00000000000000000002\nnew_highwater=00000000000000000002\nnew_"
                "version=v2\n"
                "new_sha256=1111111111111111111111111111111111111111111111111111111111111111\n");
        break;
    case GamepadWire: {
        nbt_gamepad_wire_state st = {};
        uint8_t data[NBT_GAMEPAD_WIRE_SIZE];
        st.connected = 1;
        (void)nbt_gamepad_wire_encode(&st, data, sizeof(data));
        out.push_back(std::vector<uint8_t>(data, data + sizeof(data)));
        break;
    }
    case GamepadGip: {
        uint8_t data[18] = {0x20, 0, 1, 14};
        out.push_back(std::vector<uint8_t>(data, data + sizeof(data)));
        break;
    }
    case GamepadSony: {
        uint8_t data[64] = {1, 128, 128, 128, 128, 8, 0, 0, 8};
        out.push_back(std::vector<uint8_t>(data, data + sizeof(data)));
        break;
    }
    case MostCodec:
    case MostJoiner: {
        std::vector<uint8_t> bytes(32, 0);
        bytes[28] = 0x46;
        bytes[29] = 0x0c;
        out.push_back(bytes);
        bytes[4] = 2;
        bytes[5] = 0;
        bytes[8] = 1;
        bytes[16] = 19;
        out.push_back(bytes);
        break;
    }
    case Ccs: {
        const uint8_t rotary[] = {1, 0, 9, 0, 0, 0, 0, 0xff};
        const uint8_t keys[] = {1, 0, 0, 0, 0, 0, 0};
        out.push_back(std::vector<uint8_t>(rotary, rotary + sizeof(rotary)));
        out.push_back(std::vector<uint8_t>(keys, keys + sizeof(keys)));
        break;
    }
    case MostPolicy:
        addSeed(&out, "rx:05.00.621,rx:E5.*.*,tx:82.00.*,ctl:E5.03");
        addSeed(&out, "sub:05.00.621,rx:*.*.*");
        addSeed(&out, "rx:05.00.621,sub:05.*.621,tx:05.00.001");
        break;
    case FilesPolicy:
        addSeed(&out, "sfc,smc,fig,swc|16|1024");
        addSeed(&out, "cue,bin,chd,m3u|700|8000|folders=m3u,cue");
        addSeed(&out, "a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p|4096|65536");
        addSeed(&out, "Super Game (Europe) [!].sfc");
        break;
    case BridgeRequirement:
        addSeed(&out, "bridge=1.1\nbridge-uses=most\nbridge-optional=show\n"
                      "bridge-most=rx:05.00.621,tx:82.00.*,ctl:E5.03\n");
        addSeed(&out, "bridge=1.2\nbridge-uses=most\nbridge-optional=\n"
                      "bridge-most=rx:05.00.621,sub:05.00.621\n");
        addSeed(&out, "bridge=1.5\nbridge-uses=files\nbridge-optional=most\n"
                      "bridge-most=rx:05.00.621\nbridge-files=sfc,smc|16|1024\n");
        addSeed(&out, "bridge=1.6\nbridge-uses=status\nbridge-optional=hold,car-data,net\n"
                      "bridge-cds=driving.gear,hmi.graphicalContext\n"
                      "bridge-net=tcp:5277@uap0+wfd0\n");
        break;
    case CdsPolicy:
        addSeed(&out, "driving.gear,sensors.PDCStatus,vehicle.dayNightChangeOver");
        addSeed(&out, "a.b,c..d,.e,f.");
        break;
    case NetPolicy:
        addSeed(&out, "tcp:5277@uap0+wfd0");
        addSeed(&out, "tcp:1@uap0,tcp:65535@wfd0,tcp:80@uap0+wfd0,tcp:8080@wfd0");
        break;
    case ZipArchiveTarget:
        out.push_back(zipSeed(0));
        out.push_back(zipSeed(8));
        break;
    case BridgeHold:
        addSeed(&out, "1700000000000 start android-auto\n");
        addSeed(&out, "12 hold\n");
        addSeed(&out, "1700000000000 12 android-auto peek\n");
        addSeed(&out, "12 aside\n");
        addSeed(&out, "7 {\"gear\":2}\n");
        break;
    case BridgeStatus:
        addSeed(&out, "00000000000000000001 ready\n");
        break;
    case WfdMost: {
        const uint8_t cfg[] = {2,   '0', 0, 2, 'B', 0,   2,   '1', 0,   2,   0,   2,   0,   1, 2,
                               'D', 0,   1, 4, 2,   'P', 'a', 's', 's', 'W', 'o', 'r', 'd', 0};
        out.push_back(std::vector<uint8_t>(cfg, cfg + sizeof(cfg)));
        break;
    }
    case CaptureControl:
        addSeed(&out, "v1 pid=42 sid=1 gen=2 st=1 k=03 c=01 inst=01,03 kb=1 cb=1 wd=1500 esc=1\n");
        break;
    case CaptureStream: {
        std::vector<uint8_t> pkt(55, 0);
        pkt[8] = 0x01;
        pkt[11] = 0x80;
        pkt[16 + 4] = 2;
        pkt[16 + 8] = 1;
        pkt[16 + 16] = 0xE5;
        pkt[16 + 17] = 0x03;
        pkt[16 + 18] = 0x47;
        pkt[16 + 19] = 0x0C;
        pkt[16 + 20] = 0x0C;
        pkt[16 + 28] = 7;
        pkt[16 + 33] = 1;
        out.push_back(pkt);
        break;
    }
    case StarterConfigTarget:
        addSeed(&out,
                "<Number>6</Number>\r\n<Name>System_HMI_PRELOAD</Name>\r\n"
                "<EnvName>LD_PRELOAD</EnvName>\r\n<Value>libEGL_X.gpp.so:"
                "libGLESv2_X_FORWARD.gpp.so:libKD_X.gpp.so</Value>\r\n<Overwrite>1</Overwrite>");
        break;
    case LanguageTarget:
        addSeed(&out, "{\"language\":13}");
        addSeed(&out, "Package rejected: signature \xe4\xb8\xad\xe6\x96\x87");
        addSeed(&out, "NBT Manager v3 starts when App Manager opens again");
        break;
    }
    return out;
}

nbt::core::Span<const uint8_t> bytes(const std::vector<uint8_t> &input) {
    return nbt::core::Span<const uint8_t>(input.empty() ? NULL : &input[0], input.size());
}

void copyText(const std::vector<uint8_t> &input, char *output) {
    if (!input.empty()) {
        memcpy(output, &input[0], input.size());
    }
    output[input.size()] = 0;
}

void runOne(Target target, const std::vector<uint8_t> &input, FILE *scratch,
            nbt_most_joiner *joiner, nbt_ccs *ccs) {
    char text[kInputMax + 1];
    switch (target) {
    case Http: {
        nbt::worker::http::Request request;
        (void)nbt::worker::http::parseRequest(bytes(input), &request);
        break;
    }
    case PhoneSignature:
        if (input.size() >= 97 && input.size() <= 169) {
            (void)nbt::crypto::EcPublicKey::verify(
                &input[0], nbt::core::Span<const uint8_t>(&input[65], 32),
                nbt::core::Span<const uint8_t>(&input[97], input.size() - 97));
        }
        break;
    case PhonePair: {
        copyText(input, text);
        nbt::storage::Phone phone;
        (void)nbt::worker::PhoneActions::parsePair(text, phone);
        break;
    }
    case PhoneContext: {
        copyText(input, text);
        unsigned speed = 0;
        nbt::gui::PhoneWake wake;
        (void)nbt::gui::phoneWakeParse(text, input.size(), wake);
        (void)nbt::gui::phoneContextSafe(text);
        (void)nbt::gui::phoneSpeed(text, speed);
        break;
    }
    case Control: {
        if (input.size() > 2048) {
            break;
        }
        int descriptors[2];
        if (pipe(descriptors)) {
            break;
        }
        nbt::core::UniqueFd reader(descriptors[0]);
        nbt::core::UniqueFd writer(descriptors[1]);
        size_t written = 0;
        while (written < input.size()) {
            const ssize_t count = write(writer.get(), &input[written], input.size() - written);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                break;
            }
            written += static_cast<size_t>(count);
        }
        writer.reset();
        Commands commands;
        nbt::worker::ControlProtocol protocol(reader.get());
        for (unsigned i = 0; i < 4 && protocol.read(commands, 1).ok(); ++i) {
        }
        break;
    }
    case PackageHeader:
        if (scratch && !ftruncate(fileno(scratch), 0) && !fseek(scratch, 0, SEEK_SET) &&
            (input.empty() || fwrite(&input[0], 1, input.size(), scratch) == input.size()) &&
            !fflush(scratch) && !fseek(scratch, 0, SEEK_SET)) {
            (void)nbt::package::PackageVerifier::peekApp(fileno(scratch));
        }
        break;
    case PackageManifest:
        (void)nbt::package::PackageVerifier::parseManifestForTesting(bytes(input), 52);
        break;
    case ReleaseManifest:
        (void)nbt::package::SignedReleaseVerifier::parseManifestForTesting(
            bytes(input), NBT_RELEASE_V1_COMPONENTS);
        (void)nbt::package::SignedReleaseVerifier::parseManifestForTesting(
            bytes(input), NBT_RELEASE_MAX_COMPONENTS);
        break;
    case SettingsSchema: {
        nbt_settings settings;
        (void)nbt_settings_schema_parse(input.empty() ? ""
                                                      : reinterpret_cast<const char *>(&input[0]),
                                        input.size(), &settings);
        break;
    }
    case SettingsValues: {
        nbt_settings settings;
        memset(&settings, 0, sizeof(settings));
        (void)nbt_settings_add(&settings, "mode|Mode|on|on,off");
        (void)nbt_settings_values_apply(
            &settings, input.empty() ? "" : reinterpret_cast<const char *>(&input[0]),
            input.size());
        break;
    }
    case ControllerConfig: {
        nbt_ccs_config config;
        (void)nbt_controller_config_parse(
            input.empty() ? "" : reinterpret_cast<const char *>(&input[0]), input.size(), &config);
        break;
    }
    case InstalledRecord:
        copyText(input, text);
        (void)nbt::model::InstalledRecord::parse(nbt::core::Span<const char>(text, input.size()));
        break;
    case Journal:
        copyText(input, text);
        (void)nbt::lifecycle::AppTransactions::parseJournalForTesting(
            nbt::core::Span<const char>(text, input.size()));
        break;
    case MostCodec: {
        nbt_most_message message;
        (void)nbt_most_parse_any(input.empty() ? NULL : &input[0], input.size(), &message);
        break;
    }
    case MostJoiner: {
        nbt_most_message message;
        nbt_most_message joined;
        const int kind =
            nbt_most_parse_any(input.empty() ? NULL : &input[0], input.size(), &message);
        if (kind >= 0) {
            (void)nbt_most_join(joiner, &message, static_cast<nbt_most_class>(kind), &joined);
        }
        break;
    }
    case Ccs: {
        nbt_input actions[16];
        (void)nbt_ccs_feed(ccs, 0xe5, 3, 0xc46, 0x0c, input.empty() ? NULL : &input[0],
                           input.size(), actions, sizeof(actions) / sizeof(actions[0]));
        (void)nbt_ccs_feed(ccs, 0xe5, 1, 0xc10, 0x0c, input.empty() ? NULL : &input[0],
                           input.size(), actions, sizeof(actions) / sizeof(actions[0]));
        break;
    }
    case MostPolicy: {
        nbt_most_policy policy;
        copyText(input, text);
        (void)nbt_most_policy_parse(text, &policy);
        break;
    }
    case FilesPolicy: {
        // The policy, the names a car stores, and a stick's names made stored names.
        nbt_files_policy policy;
        char name[NBT_FILES_NAME_MAX + 1];
        copyText(input, text);
        if (!nbt_files_policy_parse(text, &policy)) {
            (void)nbt_files_policy_accepts(&policy, "game.sfc");
            (void)nbt_files_policy_marks_folder(&policy, "game.cue");
            (void)nbt_files_policy_quota_bytes(&policy);
        }
        (void)nbt_files_name_valid(text);
        (void)nbt_files_listed_name_valid(text);
        if (nbt::worker::UsbFiles::storedName(text, name) && !nbt_files_name_valid(name)) {
            abort(); // a stored name is always valid
        }
        char folder[NBT_FILES_FOLDER_MAX + 1];
        if (nbt::worker::UsbFiles::storedFolderName(text, strlen(text), folder) &&
            !nbt_files_folder_valid(folder)) {
            abort(); // a stored folder name too
        }
        break;
    }
    case CdsPolicy: {
        nbt_cds_policy policy;
        copyText(input, text);
        (void)nbt_cds_policy_parse(text, &policy);
        (void)nbt_cds_name_valid(text, strlen(text));
        break;
    }
    case NetPolicy: {
        nbt_net_policy policy;
        copyText(input, text);
        (void)nbt_net_policy_parse(text, &policy);
        break;
    }
    case BridgeHold: {
        // The four bridge 1.6 lines, as raw bytes (no NUL): each parsed line formats back.
        const char *line = input.empty() ? "" : reinterpret_cast<const char *>(&input[0]);
        uint64_t ms = 0, sequence = 0;
        nbt_hold_word word = NBT_HOLD_RELEASE;
        nbt_hold_state state = NBT_HOLD_HELD;
        char app[NBT_HOLD_APP_MAX + 1], value[NBT_CDS_VALUE_MAX + 1], out[NBT_CDS_LINE_MAX + 1];
        if (!nbt_hold_request_parse(line, input.size(), &sequence, &word) &&
            nbt_hold_request_format(sequence, word, out, sizeof(out)) < 0) {
            abort();
        }
        if (!nbt_hold_relay_parse(line, input.size(), &ms, &sequence, app, &word) &&
            nbt_hold_relay_format(ms, sequence, app, word, out, sizeof(out)) < 0) {
            abort();
        }
        if (!nbt_hold_state_parse(line, input.size(), &sequence, &state) &&
            nbt_hold_state_format(sequence, state, out, sizeof(out)) < 0) {
            abort();
        }
        if (!nbt_cds_value_parse(line, input.size(), &sequence, value) &&
            nbt_cds_value_format(sequence, value, out, sizeof(out)) < 0) {
            abort();
        }
        nbt::gui::ShortcutRecord shortcut; // the App shortcut record: parsed formats back
        if (nbt::gui::shortcutRecordParse(line, input.size(), shortcut) &&
            nbt::gui::shortcutRecordFormat(shortcut, out, sizeof(out)) < 0) {
            abort();
        }
        break;
    }
    case ZipArchiveTarget: {
        // A stick's archive: listed, then each entry unpacked (into /dev/null).
        char path[64];
        snprintf(path, sizeof(path), "/tmp/nbt-fuzz-zip-%d", static_cast<int>(getpid()));
        const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        const bool written = fd >= 0 && (input.empty() || write(fd, &input[0], input.size()) ==
                                                              static_cast<ssize_t>(input.size()));
        if (fd >= 0) {
            close(fd);
        }
        nbt::storage::ZipArchive zip;
        const int sink = open("/dev/null", O_WRONLY);
        if (written && sink >= 0 && zip.open(path)) {
            for (unsigned i = 0; i < zip.count(); ++i) {
                (void)nbt::storage::ZipArchive::baseName(zip.entry(i).name);
                (void)zip.extract(i, sink, NULL, NULL);
            }
        }
        if (sink >= 0) {
            close(sink);
        }
        unlink(path);
        break;
    }
    case BridgeRequirement: {
        nbt_bridge_requirement requirement;
        copyText(input, text);
        char *cursor = text;
        (void)nbt_bridge_requirement_parse(&cursor, "", &requirement);
        break;
    }
    case GamepadWire: {
        nbt::gamepad::Input reader;
        (void)reader.feed(bytes(input).data(), input.size(), 100);
        nbt::gamepad::Navigation navigation;
        (void)navigation.update(reader.state(), 100);
        break;
    }
    case GamepadGip: {
        nbt::gamepad::GipSession session;
        session.reset(0x045e, 0x02ea);
        nbt_gamepad_wire_state st = {};
        (void)session.feed(bytes(input).data(), input.size(), st);
        uint8_t packet[64];
        for (unsigned i = 0; i < 32; ++i) {
            (void)session.output(packet);
        }
        (void)session.feed(bytes(input).data(), input.size(), st);
        break;
    }
    case GamepadSony: {
        nbt_gamepad_wire_state st = {};
        (void)nbt::gamepad::decodeSony(nbt::gamepad::DualShock4, bytes(input).data(), input.size(),
                                       st);
        (void)nbt::gamepad::decodeSony(nbt::gamepad::DualSense, bytes(input).data(), input.size(),
                                       st);
        break;
    }
    case BridgeStatus: {
        uint64_t sequence;
        char status[NBT_BRIDGE_STATUS_MAX + 1];
        copyText(input, text);
        (void)nbt_bridge_status_parse(text, input.size(), &sequence, status);
        break;
    }
    case WfdMost: {
        nbt_most_wire_message msg;
        memset(&msg, 0, sizeof(msg));
        msg.fblock = nbt::worker::kMostFblockWlan;
        msg.inst = nbt::worker::kMostInstWfd;
        msg.data = bytes(input).data();
        msg.length = static_cast<uint32_t>(input.size());
        msg.fkt = nbt::worker::kFktWlanState3;
        msg.op = nbt::worker::kMostOpStatus;
        nbt::worker::WfdStateReply st;
        (void)nbt::worker::decodeWfdState(msg, &st);
        msg.fkt = nbt::worker::kFktSelectWlanState3;
        msg.op = nbt::worker::kMostOpResultAck;
        nbt::worker::WfdSelectReply sel;
        (void)nbt::worker::decodeWfdSelect(msg, &sel);
        msg.fkt = nbt::worker::kFktWlanConfiguration;
        msg.op = nbt::worker::kMostOpStatus;
        nbt::worker::WfdConfig cfg;
        (void)nbt::worker::decodeWfdConfig(msg, &cfg);
        (void)nbt::worker::validWfdSsid(cfg.ssid);
        (void)nbt::worker::validWfdPassphrase(cfg.passphrase.data(), cfg.passphrase.size());
        break;
    }
    case CaptureControl: {
        nbt_capture_control ctrl;
        copyText(input, text);
        if (nbt_capture_control_parse(text, input.size(), &ctrl) == 0) {
            char formatted[NBT_CAPTURE_CONTROL_MAX];
            (void)nbt_capture_control_format(&ctrl, formatted, sizeof(formatted));
        }
        break;
    }
    case CaptureStream: {
        nbt_capture_control ctrl;
        nbt_capture_control_init(&ctrl, 1, 1u, 1u, NULL);
        ctrl.state = NBT_CAPTURE_ON;
        nbt_capture_packet pkt;
        nbt_dsi_c_stream s;
        memset(&s, 0, sizeof(s));
        s.capacity = static_cast<uint32_t>(input.size());
        s.populated = static_cast<uint32_t>(input.size());
        s.buffer = bytes(input).data();
        (void)nbt_capture_inspect_stream(&s, &ctrl, &pkt);
        break;
    }
    case StarterConfigTarget: {
        const char *data = input.empty() ? "" : reinterpret_cast<const char *>(&input[0]);
        const nbt::storage::StarterConfigState st =
            nbt::storage::StarterConfig::inspect(data, input.size(), NULL);
        if (st != nbt::storage::StarterInvalid) {
            std::vector<char> out(input.size() + 128);
            size_t outLen = 0;
            if (nbt::storage::StarterConfig::buildModified(data, input.size(), true, &out[0],
                                                           out.size(), &outLen)) {
                (void)nbt::storage::StarterConfig::matchesOriginOutsidePreload(&out[0], outLen,
                                                                               data, input.size());
            }
            (void)nbt::storage::StarterConfig::buildModified(data, input.size(), false, &out[0],
                                                             out.size(), &outLen);
        }
        break;
    }
    case LanguageTarget: {
        // iDrive's value, the setting, and texts cut into buffers of every size.
        copyText(input, text);
        unsigned value = 0;
        int choice = 0;
        (void)nbt::gui::carLanguageParse(text, value);
        (void)nbt::model::languageSettingParse(text, choice);
        const size_t size = input.empty() ? 1u : 1u + static_cast<size_t>(input[0]) % 160u;
        char out[160];
        const nbt::model::Language language =
            static_cast<nbt::model::Language>(input.size() % nbt::model::kLanguageCount);
        nbt::gui::setCurrentLanguage(language);
        nbt::gui::workerText(out, size, text);
        if (strlen(out) >= size) {
            abort();
        }
        nbt::gui::textf(out, size,
                        static_cast<nbt::gui::TextId>(input.size() % nbt::gui::kTextCount), text,
                        text + strlen(text) / 2);
        (void)nbt::gui::utf8Fit(out, sizeof(out), size);
        nbt::gui::utf8TrimEnd(text);
        nbt::gui::setCurrentLanguage(nbt::model::LangEn);
        break;
    }
    }
}

uint64_t monotonicMs() {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
        return 0;
    }
    return static_cast<uint64_t>(now.tv_sec) * 1000 + static_cast<uint64_t>(now.tv_nsec) / 1000000;
}

bool writeAll(int fd, const std::vector<uint8_t> &input) {
    size_t written = 0;
    while (written < input.size()) {
        const ssize_t count = write(fd, &input[written], input.size() - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        written += static_cast<size_t>(count);
    }
    return true;
}

bool failurePath(const char *target, char *path, size_t size) {
    const char *root = getenv("NBT_FUZZ_CORPUS_DIR");
    if (!root) {
        root = "tests/corpus";
    }
    char directory[1024];
    if (snprintf(directory, sizeof(directory), "%s/%s", root, target) >=
            static_cast<int>(sizeof(directory)) ||
        (mkdir(root, 0700) && errno != EEXIST) || (mkdir(directory, 0700) && errno != EEXIST) ||
        snprintf(path, size, "%s/failure.bin", directory) >= static_cast<int>(size)) {
        return false;
    }
    return true;
}

bool saveFailure(const char *target, const std::vector<uint8_t> &input) {
    char path[1088];
    if (!failurePath(target, path, sizeof(path))) {
        return false;
    }
    nbt::core::UniqueFd fd(open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600));
    return fd.valid() && writeAll(fd.get(), input);
}

void clearFailure(const char *target) {
    char path[1088];
    if (failurePath(target, path, sizeof(path)) && unlink(path) && errno != ENOENT) {
        fprintf(stderr, "cannot clear fuzz failure file: %s\n", path);
        exit(1);
    }
}

bool replay(const char *target, const char *path) {
    nbt::core::UniqueFd fd(open(path, O_RDONLY));
    std::vector<uint8_t> input;
    uint8_t buffer[4096];
    if (!fd.valid()) {
        return false;
    }
    for (;;) {
        const ssize_t count = read(fd.get(), buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 || input.size() + static_cast<size_t>(count) > kInputMax) {
            return false;
        }
        input.insert(input.end(), buffer, buffer + count);
        if (!count) {
            break;
        }
    }
    FILE *scratch = tmpfile();
    nbt_most_joiner joiner;
    nbt_ccs ccs;
    nbt_most_join_init(&joiner);
    nbt_ccs_init(&ccs, NULL);
    const Target found = kTargets[0].target;
    Target selected = found;
    bool matched = false;
    for (size_t i = 0; i < sizeof(kTargets) / sizeof(kTargets[0]); ++i) {
        if (!strcmp(target, kTargets[i].name)) {
            selected = kTargets[i].target;
            matched = true;
        }
    }
    if (!matched || (selected == PackageHeader && !scratch)) {
        return false;
    }
    runOne(selected, input, scratch, &joiner, &ccs);
    if (scratch) {
        fclose(scratch);
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "--replay")) {
        return replay(argv[2], argv[3]) ? 0 : 2;
    }
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: fuzz-test <target> [seconds] | --replay <target> <input>\n");
        return 2;
    }
    Target selected = Http;
    bool matched = false;
    for (size_t i = 0; i < sizeof(kTargets) / sizeof(kTargets[0]); ++i) {
        if (!strcmp(argv[1], kTargets[i].name)) {
            selected = kTargets[i].target;
            matched = true;
        }
    }
    if (!matched) {
        return 2;
    }
    unsigned seconds = 1;
    if (argc == 3) {
        char *end = NULL;
        const unsigned long value = strtoul(argv[2], &end, 10);
        if (!end || *end || !value || value > 3600) {
            return 2;
        }
        seconds = static_cast<unsigned>(value);
    }
    FILE *scratch = selected == PackageHeader ? tmpfile() : NULL;
    if (selected == PackageHeader && !scratch) {
        return 1;
    }
    std::vector<std::vector<uint8_t>> corpus = seeds(selected);
    if (corpus.empty()) {
        return 1;
    }
    Mutator mutator(0x9e3779b9u ^ static_cast<uint32_t>(selected * 7919));
    nbt_most_joiner joiner;
    nbt_ccs ccs;
    nbt_most_join_init(&joiner);
    nbt_ccs_init(&ccs, NULL);
    const uint64_t started = monotonicMs();
    unsigned long iterations = 0;
    do {
        std::vector<uint8_t> input = corpus[iterations % corpus.size()];
        mutator.mutate(&input);
        if (!saveFailure(argv[1], input)) {
            return 1;
        }
        runOne(selected, input, scratch, &joiner, &ccs);
        clearFailure(argv[1]);
        ++iterations;
        if (iterations % 256 == 0) {
            nbt_most_join_init(&joiner);
            nbt_ccs_reset(&ccs);
        }
    } while (monotonicMs() - started < static_cast<uint64_t>(seconds) * 1000);
    if (scratch) {
        fclose(scratch);
    }
    printf("%s: %lu iterations\n", argv[1], iterations);
    return 0;
}

// SPDX-License-Identifier: MPL-2.0
#include "nbt/core/secure.hpp"
#include "nbt/core/system.hpp"
#include "nbt/gui/qr_code.hpp"
#include "nbt/gui/ui_controller.hpp"
#include "nbt/gui/ui_renderer.hpp"
#include "nbt/gui/worker_client.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { W = 640, H = 240 };
static unsigned char pixels[W * H * 4];

static int pump(nbt::gui::UiController &ui, nbt::gui::WorkerClient &client) {
    char reply[nbt::gui::kReplyMax];
    for (;;) {
        const nbt::core::Result<uint64_t> now = nbt::core::monotonicMs();
        if (!now.get()) {
            return -1;
        }
        nbt::gui::Request request;
        const int next = ui.nextRequest(now.value(), request);
        if (next <= 0) {
            return next;
        }
        if (!client
                 .request(request.command, nbt::core::Span<char>(reply, sizeof(reply)),
                          request.timeoutMs)
                 .ok()) {
            ui.requestFailed();
        } else {
            const nbt::core::Result<uint64_t> replied = nbt::core::monotonicMs();
            if (!replied.get()) {
                return -1;
            }
            ui.reply(reply, replied.value());
        }
    }
}

static int wait_ms(unsigned long ms) {
    struct timespec pauseTime;
    pauseTime.tv_sec = static_cast<time_t>(ms / 1000u);
    pauseTime.tv_nsec = static_cast<long>(ms % 1000u) * 1000000L;
    return nanosleep(&pauseTime, NULL);
}

static int show(const nbt::gui::UiController &ui) {
    nbt::gui::UiView view;
    nbt::gui::Frame frame = {pixels, sizeof(pixels), W, H, W * 4};
    nbt::gui::QrCode qr;
    nbt::gui::UiRenderer renderer;
    char link[160] = {0};
    if (!ui.view(view).ok()) {
        return -1;
    }
    bool withQr = false;
    if (view.screen == nbt::gui::Pairing &&
        ui.pairingLink(nbt::core::Span<char>(link, sizeof(link))).ok()) {
        withQr = qr.encode(reinterpret_cast<const unsigned char *>(link), strlen(link), -1).ok();
    }
    if (view.screen == nbt::gui::Pairing && !withQr) {
        return -1;
    }
    if (!renderer.render(view, withQr ? &qr : NULL, frame).ok()) {
        return -1;
    }
    printf("VIEW\t%d\t%d\t%zu\t%d\t%s\t", static_cast<int>(view.screen), view.busy, view.focus,
           ui.wantsExit(), view.title);
    for (size_t i = 0; i < view.lineCount; ++i) {
        printf("%s%s", i ? "|" : "", view.lines[i]);
    }
    putchar('\t');
    for (size_t i = 0; i < view.itemCount; ++i) {
        printf("%s%s", i ? "|" : "", view.items[i]);
    }
    if (view.modal.shown) {
        printf("\t%s\t%s\t%d\t", view.modal.title, view.modal.text, view.modal.progress);
        for (size_t i = 0; i < view.modal.itemCount; ++i) {
            printf("%s%s", i ? "|" : "", view.modal.items[i]);
        }
        printf("\t%zu", view.modal.focus);
    } else {
        printf("\t\t\t-1\t\t0");
    }
    putchar('\n');
    return fflush(stdout) ? -1 : 0;
}

int main(int argc, char **argv) {
    nbt::gui::WorkerClient client;
    nbt::gui::UiController ui;
    char line[64], reply[64];
    int rc = 1;
    if (argc != 3) {
        return 2;
    }
    if (!ui.init("lo", 0).ok() || !client.spawnTest(argv[1], argv[2]).ok()) {
        return 1;
    }
    if (pump(ui, client) || show(ui)) {
        goto done;
    }
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (!strcmp(line, "up")) {
            ui.press(nbt::gui::KeyUp);
        } else if (!strcmp(line, "down")) {
            ui.press(nbt::gui::KeyDown);
        } else if (!strcmp(line, "select")) {
            ui.press(nbt::gui::KeySelect);
            if (pump(ui, client)) {
                goto done;
            }
            ui.release(nbt::gui::KeySelect);
        } else if (!strcmp(line, "hold")) {
            ui.press(nbt::gui::KeySelect);
            for (unsigned waited = 0; waited <= nbt::gui::kHoldMs + 200u; waited += 100u) {
                if (pump(ui, client) || wait_ms(100)) {
                    goto done;
                }
            }
        } else if (!strcmp(line, "option")) {
            ui.press(nbt::gui::KeyOption);
        } else if (!strcmp(line, "release")) {
            ui.release(nbt::gui::KeySelect);
        } else if (!strcmp(line, "back")) {
            ui.press(nbt::gui::KeyBack);
        } else if (!strcmp(line, "poll")) {
            if (wait_ms(nbt::gui::kPollMs + 50u)) {
                goto done;
            }
        } else if (!strncmp(line, "snapshot ", 9)) {
            FILE *out = fopen(line + 9, "wb");
            if (!out || fprintf(out, "P5\n%d %d\n255\n", W, H) < 0) {
                if (out) {
                    fclose(out);
                }
                goto done;
            }
            for (size_t p = 0; p < static_cast<size_t>(W) * H; ++p) {
                fputc(pixels[p * 4], out);
            }
            if (fclose(out)) {
                goto done;
            }
        } else if (!strcmp(line, "address")) {
            printf("ADDRESS\t%s\n", ui.host);
        } else if (!strcmp(line, "link")) {
            char link[160];
            if (!ui.pairingLink(nbt::core::Span<char>(link, sizeof(link))).ok()) {
                goto done;
            }
            printf("LINK\t%s\n", link);
        } else {
            goto done;
        }
        if (pump(ui, client) || show(ui)) {
            goto done;
        }
    }
    nbt::core::secureClear(pixels, sizeof(pixels));
    rc = client.connected() &&
                 client.request("QUIT", nbt::core::Span<char>(reply, sizeof(reply)), 5000).ok() &&
                 !strcmp(reply, "OK")
             ? 0
             : 1;
done:
    ui.clearSecrets();
    client.close();
    return rc;
}

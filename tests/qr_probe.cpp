/* Prints a QR symbol for stdin data: "version mask size" then one row of 0/1
 * per line. argv[1] is the mask (-1 selects automatically). */
#include "nbt/gui/qr_code.hpp"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    unsigned char data[512];
    size_t length;
    unsigned x, y;
    nbt::gui::QrCode qr;
    if (argc != 2) {
        return 2;
    }
    length = fread(data, 1, sizeof(data), stdin);
    if (ferror(stdin) || !qr.encode(data, length, atoi(argv[1])).ok()) {
        return 1;
    }
    printf("%u %u %u\n", qr.version_, qr.mask_, qr.size());
    for (y = 0; y < qr.size(); ++y) {
        for (x = 0; x < qr.size(); ++x) {
            putchar(qr.dark(x, y) ? '1' : '0');
        }
        putchar('\n');
    }
    return 0;
}

import os
from pathlib import Path
import subprocess
import unittest


BUILD = Path(os.environ.get("NBT_BUILD_DIR", Path(__file__).resolve().parents[1] / "build"))
PROBE = BUILD / "qr-probe"
# Level M: total codewords, EC codewords per block, blocks.
LEVEL_M = {1: (26, 10, 1), 2: (44, 16, 1), 3: (70, 26, 1), 4: (100, 18, 2), 5: (134, 24, 2),
           6: (172, 16, 4), 7: (196, 18, 4), 8: (242, 22, 4), 9: (292, 22, 5), 10: (346, 26, 5)}
ALIGN = {1: [], 2: [6, 18], 3: [6, 22], 4: [6, 26], 5: [6, 30], 6: [6, 34],
         7: [6, 22, 38], 8: [6, 24, 42], 9: [6, 26, 46], 10: [6, 28, 50]}
MASKS = [lambda x, y: (x + y) % 2 == 0, lambda x, y: y % 2 == 0, lambda x, y: x % 3 == 0,
         lambda x, y: (x + y) % 3 == 0, lambda x, y: (x // 3 + y // 2) % 2 == 0,
         lambda x, y: x * y % 2 + x * y % 3 == 0, lambda x, y: (x * y % 2 + x * y % 3) % 2 == 0,
         lambda x, y: ((x + y) % 2 + x * y % 3) % 2 == 0]


def encode(data, mask=-1):
    result = subprocess.run([PROBE, str(mask)], input=data, capture_output=True, check=False)
    if result.returncode:
        return None
    lines = result.stdout.decode().split("\n")
    version, mask, size = (int(v) for v in lines[0].split())
    matrix = [[c == "1" for c in line] for line in lines[1:1 + size]]
    assert all(len(row) == size for row in matrix) and len(matrix) == size
    return version, mask, matrix


def gf_mul(a, b):
    r = 0
    while b:
        if b & 1:
            r ^= a
        a <<= 1
        if a & 0x100:
            a ^= 0x11D
        b >>= 1
    return r


def syndromes_zero(block, ec):
    x = 1
    for _ in range(ec):
        value = 0
        for c in block:
            value = gf_mul(value, x) ^ c
        if value:
            return False
        x = gf_mul(x, 2)
    return True


def function_map(version):
    n = 17 + 4 * version
    f = [[False] * n for _ in range(n)]
    for y in range(n):
        for x in range(n):
            if x == 6 or y == 6:
                f[y][x] = True
            if (x < 9 and y < 9) or (x >= n - 8 and y < 9) or (x < 9 and y >= n - 8):
                f[y][x] = True
            if version >= 7 and ((x >= n - 11 and y < 6) or (y >= n - 11 and x < 6)):
                f[y][x] = True
    pos = ALIGN[version]
    for i, cy in enumerate(pos):
        for j, cx in enumerate(pos):
            if (i, j) in ((0, 0), (0, len(pos) - 1), (len(pos) - 1, 0)):
                continue
            for y in range(cy - 2, cy + 3):
                for x in range(cx - 2, cx + 3):
                    f[y][x] = True
    return f


def read(matrix):
    """Independent reader without error correction: returns (mask, payload)."""
    n = len(matrix)
    version = (n - 17) // 4
    bits = 0
    for i, (x, y) in enumerate([(8, 0), (8, 1), (8, 2), (8, 3), (8, 4), (8, 5), (8, 7), (8, 8),
                                (7, 8), (5, 8), (4, 8), (3, 8), (2, 8), (1, 8), (0, 8)]):
        bits |= matrix[y][x] << i
    second = 0
    for i in range(8):
        second |= matrix[8][n - 1 - i] << i
    for i in range(8, 15):
        second |= matrix[n - 15 + i][8] << i
    assert bits == second, "format copies differ"
    assert matrix[n - 8][8], "dark module missing"
    value = bits ^ 0x5412
    remainder = value >> 10
    for _ in range(10):
        remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537)
    assert value & 0x3FF == remainder & 0x3FF, "format BCH"
    assert value >> 13 == 0, "not level M"
    mask = (value >> 10) & 7
    f = function_map(version)
    stream = []
    right = n - 1
    while right >= 1:
        if right == 6:
            right = 5
        for vertical in range(n):
            for j in range(2):
                x = right - j
                y = n - 1 - vertical if ((right + 1) & 2) == 0 else vertical
                if not f[y][x]:
                    stream.append(matrix[y][x] ^ MASKS[mask](x, y))
        right -= 2
    total, ec, count = LEVEL_M[version]
    codewords = [int("".join(str(int(b)) for b in stream[i * 8:i * 8 + 8]), 2) for i in range(total)]
    short = count - total % count
    short_len = total // count
    data_lens = [short_len - ec + (b >= short) for b in range(count)]
    blocks = [[] for _ in range(count)]
    k = 0
    for i in range(max(data_lens)):
        for b in range(count):
            if i < data_lens[b]:
                blocks[b].append(codewords[k])
                k += 1
    for i in range(ec):
        for b in range(count):
            blocks[b].append(codewords[k])
            k += 1
    for block in blocks:
        assert syndromes_zero(block, ec), "Reed-Solomon check failed"
    data = [c for b in range(count) for c in blocks[b][:data_lens[b]]]
    bitstring = "".join(f"{c:08b}" for c in data)
    assert bitstring[:4] == "0100", "not byte mode"
    count_bits = 8 if version < 10 else 16
    length = int(bitstring[4:4 + count_bits], 2)
    start = 4 + count_bits
    payload = bytes(int(bitstring[start + i * 8:start + i * 8 + 8], 2) for i in range(length))
    return mask, payload


class QrTests(unittest.TestCase):
    LINK = (b"http://172.16.224.1:8080/"
            b"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")

    def test_round_trip_all_versions_and_masks(self):
        # Byte capacity at level M for versions 1 to 10.
        capacities = [14, 26, 42, 62, 84, 106, 122, 152, 180, 213]
        for version, capacity in enumerate(capacities, 1):
            for length in (capacity, capacity + 1):
                data = bytes((i * 37 + length) % 256 for i in range(length))
                result = encode(data)
                if length == 213 + 1:
                    self.assertIsNone(result)
                    continue
                self.assertEqual(result[0], version if length == capacity else version + 1)
                self.assertEqual(read(result[2]), (result[1], data))
        for mask in range(8):
            version, chosen, matrix = encode(self.LINK, mask)
            self.assertEqual((version, chosen), (6, mask))
            self.assertEqual(read(matrix), (mask, self.LINK))

    def test_empty_and_invalid_input(self):
        version, mask, matrix = encode(b"")
        self.assertEqual((version, len(matrix)), (1, 21))
        self.assertEqual(read(matrix)[1], b"")
        self.assertIsNone(encode(b"x", 8))
        self.assertIsNone(encode(b"x", -2))

    def test_matches_independent_encoder(self):
        try:
            import segno
        except ImportError:
            self.skipTest("segno is not installed")
        # segno 1.6.1 inserts a zero byte when the terminator already ends on a
        # codeword boundary (ISO/IEC 18004 7.4.10 adds no bits then), so only
        # symbols filled to capacity, which need no padding, are compared.
        for capacity in (14, 26, 42, 62, 84, 106, 122, 152, 180, 213):
            data = bytes((i * 37 + capacity) % 256 for i in range(capacity))
            for mask in range(8):
                version, _, matrix = encode(data, mask)
                ref = segno.make(data, error="m", mode="byte", mask=mask, version=version,
                                 boost_error=False)
                expected = [[bool(v) for v in row] for row in ref.matrix]
                self.assertEqual(matrix, expected, (data[:8], mask))

    def test_scans_with_independent_decoder(self):
        try:
            import zxingcpp
            from PIL import Image
        except ImportError:
            self.skipTest("zxing-cpp or Pillow is not installed")
        # Empty payload encoding is covered above; ZXing 3 drops empty symbols.
        texts = [self.LINK, b"http://10.0.0.1:1/", b"x" * 100, b"y" * 200]
        for text in texts:
            version, mask, matrix = encode(text)
            n, scale, quiet = len(matrix), 4, 4
            image = Image.new("L", ((n + 2 * quiet) * scale,) * 2, 255)
            for y, row in enumerate(matrix):
                for x, dark in enumerate(row):
                    if dark:
                        image.paste(0, ((x + quiet) * scale, (y + quiet) * scale,
                                        (x + quiet + 1) * scale, (y + quiet + 1) * scale))
            results = zxingcpp.read_barcodes(image)
            self.assertEqual([r.text for r in results], [text.decode()], (version, mask))


if __name__ == "__main__":
    unittest.main()

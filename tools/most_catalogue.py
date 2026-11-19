#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""List the MOST functions an iDrive HmiMain3D uses, with FBlock and function ids.

    python3 -I tools/most_catalogue.py /path/to/HmiMain3D > most-functions.tsv

HmiMain3D carries generated classes for every MOST function block it controls
(`CPxy<FBlock>::C<Function>`) or implements (`CStb<FBlock>::C<Function>`).
Their names are in the exported vtable symbols; the ids are not. This tool
recovers them statically:

- slot 4 of each FBlock class vtable (`vtable for CPxy<FBlock>`) is the
  routine that initialises the function members, each with a call
  `init(this + offset, this, function_id)`;
- the FBlock constructors store each member's vtable at the same offset,
  inline or through an out-of-line member constructor;
- a registration call near each exported factory passes the FBlock id.

It reads the ELF file only and never executes it. Requirements: Python 3,
capstone (`pip install capstone`), `readelf`, and `arm-none-eabi-nm` and
`arm-none-eabi-c++filt` (or set NM and CXXFILT). Tested on NBTevo_Y21432J;
another firmware may need checking against known ids (docs/head-unit-interfaces.md).
The vendor binary and its output stay outside this repository unless the
output is reviewed as interoperability facts.
"""
import argparse
import bisect
import collections
import os
import re
import struct
import subprocess
import sys

try:
    import capstone
    from capstone import arm
except ImportError:  # pragma: no cover - environment dependent
    sys.exit("most_catalogue: the capstone Python module is required")

THIS = "this"
SP = "sp"
REG = {arm.ARM_REG_SB: 9, arm.ARM_REG_SL: 10, arm.ARM_REG_FP: 11, arm.ARM_REG_IP: 12,
       arm.ARM_REG_SP: 13, arm.ARM_REG_LR: 14, arm.ARM_REG_PC: 15}
BL_RET = 0xE12FFF1E  # bx lr


class Val:
    """A tracked register value: a constant (base None) or base + k."""
    __slots__ = ("base", "k")

    def __init__(self, base, k):
        self.base, self.k = base, k


def add(a, b):
    if a is None or b is None:
        return None
    if a.base is None and b.base is None:
        return Val(None, (a.k + b.k) & 0xFFFFFFFF)
    if b.base is None:
        return Val(a.base, a.k + b.k)
    if a.base is None:
        return Val(b.base, a.k + b.k)
    return None


def neg(a):
    return None if a is None or a.base is not None else Val(None, (-a.k) & 0xFFFFFFFF)


def regno(reg):
    if arm.ARM_REG_R0 <= reg <= arm.ARM_REG_R12:
        return reg - arm.ARM_REG_R0
    return REG.get(reg)


class Image:
    def __init__(self, path, nm_tool, filt_tool):
        self.data = open(path, "rb").read()
        d = self.data
        if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1 or struct.unpack_from("<H", d, 0x12)[0] != 40:
            raise SystemExit("most_catalogue: not a 32-bit little-endian ARM ELF file")
        phoff, shoff = struct.unpack_from("<II", d, 0x1C)
        phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from("<HHHHH", d, 0x2A)
        self.segs = []
        for i in range(phnum):
            p = struct.unpack_from("<8I", d, phoff + i * phentsize)
            if p[0] == 1:
                self.segs.append((p[2], p[1], p[4]))
        self.sections = {}
        strtab = struct.unpack_from("<10I", d, shoff + shstrndx * shentsize)[4]
        for i in range(shnum):
            sh = struct.unpack_from("<10I", d, shoff + i * shentsize)
            name = d[strtab + sh[0]:d.index(b"\0", strtab + sh[0])].decode("ascii", "replace")
            self.sections[name] = (sh[3], sh[4], sh[5])
        self.relocs = {}
        out = subprocess.run(["readelf", "-rW", path], capture_output=True, text=True, check=True).stdout
        for line in out.splitlines():
            p = line.split()
            if len(p) >= 4 and p[2] in ("R_ARM_GLOB_DAT", "R_ARM_ABS32", "R_ARM_JUMP_SLOT"):
                try:
                    self.relocs[int(p[0], 16)] = int(p[3], 16)
                except ValueError:
                    pass
        out = subprocess.run([nm_tool, "-D", "-S", "--defined-only", path], capture_output=True, text=True, check=True).stdout
        self.syms = []
        for line in out.splitlines():
            p = line.split()
            if len(p) == 4:
                self.syms.append((int(p[0], 16), int(p[1], 16), p[3]))
        names = "\n".join(s[2] for s in self.syms)
        self.demangled = subprocess.run([filt_tool], input=names, capture_output=True, text=True, check=True).stdout.split("\n")
        self.text_va, self.text_off, self.text_size = self.sections[".text"]
        self.md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        self.md.detail = True

    def off(self, va):
        for v, o, s in self.segs:
            if v <= va < v + s:
                return o + va - v
        return None

    def word(self, va):
        if va in self.relocs:
            return self.relocs[va]
        o = self.off(va)
        return None if o is None or o + 4 > len(self.data) else struct.unpack_from("<I", self.data, o)[0]

    def in_text(self, va):
        return self.text_va <= va < self.text_va + self.text_size

    def callers(self):
        """Direct ARM `bl` sites by target, from a raw scan of .text."""
        result = collections.defaultdict(list)
        text = self.data[self.text_off:self.text_off + self.text_size]
        for i in range(self.text_size // 4):
            w = struct.unpack_from("<I", text, i * 4)[0]
            if (w & 0xFF000000) == 0xEB000000:
                imm = w & 0xFFFFFF
                if imm & 0x800000:
                    imm -= 0x1000000
                site = self.text_va + i * 4
                result[site + 8 + imm * 4].append(site)
        return result

    def func_start(self, addr, back=0x4000):
        """Nearest preceding `push {..., lr}`, taken as the function start."""
        a = addr
        while a > addr - back:
            a -= 4
            w = self.word(a)
            if w is not None and (w & 0xFFFF4000) == 0xE92D4000:
                return a
        return None

    def emulate(self, start, limit=20000, trace=None):
        """Straight-line register tracking from `start` with r0 = this.

        Follows unconditional branches, stops at returns, and tracks
        constants, `this`-relative and stack-relative values. Returns the
        calls (site, target, r0, r1, r2) and stores of constants to this+k.
        Optional trace(kind, site, target/address, registers/value) observes
        calls/stores and can return a symbolic r0 for a modeled allocator.
        """
        regs = {i: None for i in range(16)}
        regs[0] = Val(THIS, 0)
        regs[13] = Val(SP, 0)
        stack, calls, stores, seen = {}, [], [], set()
        pc = start
        for _ in range(limit):
            if pc in seen:
                break
            seen.add(pc)
            o = self.off(pc)
            if o is None:
                break
            insn = next(self.md.disasm(self.data[o:o + 4], pc), None)
            if insn is None:
                break
            mn, ops = insn.mnemonic, insn.operands
            always = insn.cc in (arm.ARM_CC_AL, arm.ARM_CC_INVALID)

            def value(op):
                if op.type == arm.ARM_OP_IMM:
                    return Val(None, op.imm & 0xFFFFFFFF)
                if op.type == arm.ARM_OP_REG:
                    r = regno(op.reg)
                    if r == 15:
                        return Val(None, pc + 8)
                    return None if op.shift.type != 0 else regs.get(r)
                return None

            def dest():
                return regno(ops[0].reg) if ops and ops[0].type == arm.ARM_OP_REG else None

            def mem_addr():
                m = ops[1].mem
                base = Val(None, pc + 8) if m.base == arm.ARM_REG_PC else regs.get(regno(m.base))
                if m.index != 0:
                    idx = regs.get(regno(m.index))
                    return add(base, neg(idx) if ops[1].subtracted else idx)
                disp = -abs(m.disp) if ops[1].subtracted else m.disp
                return Val(base.base, base.k + disp) if base else None

            nxt = pc + 4
            if always and mn in ("pop", "ldm") and any(x.type == arm.ARM_OP_REG and regno(x.reg) == 15 for x in ops):
                break
            if always and mn == "bx":
                break
            if mn == "b":
                if always:
                    nxt = ops[0].imm
            elif mn == "bl" and always:
                calls.append((pc, ops[0].imm, regs[0], regs[1], regs[2]))
                result = trace("call", pc, ops[0].imm, regs.copy()) if trace else None
                for r in (0, 1, 2, 3, 12, 14):
                    regs[r] = None
                regs[0] = result
            elif mn == "blx":
                for r in (0, 1, 2, 3, 12, 14):
                    regs[r] = None
            elif not always:
                d = dest()
                if d is not None and mn not in ("str", "strb", "strh", "cmp", "cmn", "tst", "teq"):
                    regs[d] = None
            elif mn in ("mov", "movw", "mvn"):
                v = value(ops[1])
                if mn == "mvn":
                    v = Val(None, (~v.k) & 0xFFFFFFFF) if v is not None and v.base is None else None
                regs[dest()] = v
            elif mn == "movt":
                v = regs[dest()]
                regs[dest()] = Val(None, (v.k & 0xFFFF) | (ops[1].imm << 16)) if v and v.base is None else None
            elif mn in ("add", "sub"):
                a, b = (value(ops[1]), value(ops[2])) if len(ops) == 3 else (regs[dest()], value(ops[1]))
                regs[dest()] = add(a, b if mn == "add" else neg(b))
            elif mn == "ldr" and len(ops) == 2 and ops[1].type == arm.ARM_OP_MEM:
                a = mem_addr()
                d = dest()
                if a is not None and a.base is None:
                    w = self.word(a.k)
                    regs[d] = Val(None, w) if w is not None else None
                elif a is not None and a.base == SP:
                    regs[d] = stack.get(a.k)
                else:
                    regs[d] = None
                if insn.writeback and ops[1].mem.base != arm.ARM_REG_PC:
                    regs[regno(ops[1].mem.base)] = a
            elif mn in ("str", "strb", "strh") and ops[1].type == arm.ARM_OP_MEM:
                v = regs[dest()]
                base_reg = regno(ops[1].mem.base)
                if len(ops) == 3:  # post-indexed
                    a = regs.get(base_reg)
                    regs[base_reg] = add(a, value(ops[2]))
                else:
                    a = mem_addr()
                    if insn.writeback:
                        regs[base_reg] = a
                if mn == "str" and a is not None:
                    if trace:
                        trace("store", pc, a, v)
                    if a.base == THIS and v is not None and v.base is None:
                        stores.append((a.k, v.k))
                    elif a.base == SP:
                        stack[a.k] = v
            else:
                d = dest()
                if d is not None and mn not in ("cmp", "cmn", "tst", "teq", "push", "stm", "stmdb"):
                    regs[d] = None
            pc = nxt
        return calls, stores


def catalogue(img):
    vtables = {}  # address -> (kind, fblock, function or None)
    factories = []
    for (a, _size, _m), d in zip(img.syms, img.demangled):
        m = re.match(r"vtable for C(Pxy|Stb)(\w+)::C(\w+)$", d)
        if m:
            vtables[a] = m.groups()
            continue
        m = re.match(r"vtable for C(Pxy|Stb)(\w+)$", d)
        if m:
            vtables[a] = (m.group(1), m.group(2), None)
            continue
        m = re.match(r"C(Pxy|Stb)(\w+)::C(?:Pxy|Stb)\w+Factory::~", d)
        if m and img.in_text(a):
            factories.append((a, m.group(1), m.group(2)))
    factories.sort()
    if not vtables:
        raise SystemExit("most_catalogue: no CPxy/CStb vtable symbols found")

    # Slot 4 of each FBlock class vtable: its member init routine.
    slot4 = {(k, fb): img.word(a + 8 + 16) for a, (k, fb, fn) in vtables.items() if fn is None and fb != "Base"}
    init_class = {v: k for k, v in slot4.items()}

    # Member init bases: the targets called as (this+k, this, constant) from those routines.
    base_votes = collections.Counter()
    for fn in set(slot4.values()):
        if not fn or not img.in_text(fn):
            continue
        calls, _ = img.emulate(fn)
        for (_, tgt, r0, r1, r2) in calls:
            if r0 is not None and r0.base == THIS and r0.k and r1 is not None and r1.base == THIS and r1.k == 0 \
                    and r2 is not None and r2.base is None:
                base_votes[tgt] += 1
    bases = {t for t, n in base_votes.items() if n >= 20}
    print("member init bases:", ", ".join("0x%x (%d)" % (t, base_votes[t]) for t in sorted(bases)), file=sys.stderr)

    # Constructors: functions loading a function-class vtable from the GOT.
    got_va, _, got_size = img.sections[".got"]
    got_offsets = set()
    for i in range(0, got_size, 4):
        w = img.word(got_va + i)
        if w in vtables and vtables[w][2]:
            got_offsets.add(i)
    text = img.data[img.text_off:img.text_off + img.text_size]
    starts = set()
    for i in range(img.text_size // 4):
        if struct.unpack_from("<I", text, i * 4)[0] in got_offsets:
            fs = img.func_start(img.text_va + i * 4)
            if fs:
                starts.add(fs)
    emulated = {fs: img.emulate(fs) for fs in sorted(starts)}
    member_ctor = {}
    for fs, (_, stores) in emulated.items():
        at0 = {v - 8 for k, v in stores if k == 0 and (v - 8) in vtables and vtables[v - 8][2]}
        if len(at0) == 1:
            member_ctor[fs] = vtables[at0.pop()]

    # offset -> function name per FBlock class, from whole-FBlock constructors only.
    class_size = collections.Counter((v[0], v[1]) for v in vtables.values() if v[2])
    names = collections.defaultdict(dict)
    for fs, (calls, stores) in emulated.items():
        found = {}
        for k, val in stores:
            if k and (val - 8) in vtables and vtables[val - 8][2]:
                found[k] = vtables[val - 8]
        for (_, tgt, r0, _r1, _r2) in calls:
            if tgt in member_ctor and r0 is not None and r0.base == THIS and r0.k:
                found.setdefault(r0.k, member_ctor[tgt])
        per_class = collections.defaultdict(set)
        for kind, fb, fn in found.values():
            per_class[(kind, fb)].add(fn)
        for k, (kind, fb, fn) in found.items():
            if len(per_class[(kind, fb)]) >= min(3, class_size[(kind, fb)]):
                names[(kind, fb)][k] = fn

    # FBlock ids: the registration call, made with a constant FBlock id in r0
    # shortly after each exported factory.
    callers = img.callers()
    fact_addrs = [f[0] for f in factories]
    reg_votes = collections.Counter()
    for a, _k, _fb in factories:
        voted = set()
        for site in range(a, a + 0x200, 4):
            w = img.word(site)
            if w is None or (w & 0xFFFF4000) != 0xE92D4000:
                continue
            for (_, tgt, r0, _r1, _r2) in img.emulate(site)[0]:
                if r0 is not None and r0.base is None and r0.k <= 0xFF and tgt not in voted:
                    voted.add(tgt)
                    reg_votes[tgt] += 1
    fb_ids = {}
    if reg_votes:
        reg_target = reg_votes.most_common(1)[0][0]
        for site in callers.get(reg_target, []):
            fs = img.func_start(site)
            if not fs:
                continue
            for (cpc, tgt, r0, _r1, _r2) in img.emulate(fs)[0]:
                if tgt == reg_target and r0 is not None and r0.base is None and r0.k <= 0xFF:
                    i = bisect.bisect_right(fact_addrs, cpc) - 1
                    if i >= 0 and cpc - fact_addrs[i] < 0x400:
                        fb_ids[(factories[i][1], factories[i][2])] = r0.k

    def gather(fn, depth, members, visited):
        if not fn or fn in visited or depth > 3 or not img.in_text(fn):
            return
        visited.add(fn)
        for (_, tgt, r0, r1, r2) in img.emulate(fn)[0]:
            if r0 is None or r0.base != THIS:
                continue
            if tgt in bases:
                if r0.k and r1 is not None and r1.base == THIS and r1.k == 0 and r2 is not None and r2.base is None:
                    members[r0.k] = r2.k
            elif r0.k == 0:
                gather(tgt, depth + 1, members, visited)

    rows, missing = [], []
    for key, fn in sorted(slot4.items()):
        members, visited = {}, set()
        gather(fn, 0, members, visited)
        if not members:
            missing.append(key)
            continue
        lookups = [names.get(key, {})] + [names.get(init_class[v], {}) for v in visited
                                          if v in init_class and init_class[v] != key]
        fid = fb_ids.get(key, fb_ids.get(("Pxy", key[1])))
        for k, fkt in sorted(members.items(), key=lambda x: x[1]):
            name = next((u[k] for u in lookups if k in u), "")
            rows.append((key[0], key[1], fid, fkt, name))
    return rows, missing


STANDARD = {0x000: "(FktIDs)", 0x001: "(Notification)", 0x002: "(NotificationCheck)"}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("hmimain", help="path to an HmiMain3D binary (read only)")
    ap.add_argument("-o", "--output", help="write the TSV here instead of standard output")
    args = ap.parse_args()
    img = Image(args.hmimain, os.environ.get("NM", "arm-none-eabi-nm"), os.environ.get("CXXFILT", "arm-none-eabi-c++filt"))
    rows, missing = catalogue(img)
    out = open(args.output, "w") if args.output else sys.stdout
    out.write("kind\tfblock\tfblock_id\tfunction_id\tfunction\n")
    for kind, fb, fid, fkt, name in sorted(rows, key=lambda r: (r[2] if r[2] is not None else 0x100, r[1], r[0], r[3])):
        out.write("%s\t%s\t%s\t0x%03X\t%s\n" % ("proxy" if kind == "Pxy" else "stub", fb,
                                                "0x%02X" % fid if fid is not None else "",
                                                fkt, name or STANDARD.get(fkt, "")))
    if out is not sys.stdout:
        out.close()
    named = sum(1 for r in rows if r[4] or r[3] in STANDARD)
    print("%d functions in %d FBlock classes, %d named; no members reached for: %s" % (
        len(rows), len({(r[0], r[1]) for r in rows}), named,
        ", ".join("%s%s" % k for k in missing) or "none"), file=sys.stderr)


if __name__ == "__main__":
    main()

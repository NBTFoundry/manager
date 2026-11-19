#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Read a QNX 6.5 ARM core file from the head unit's dumper (docs/car-diagnostics.md).

Prints the process id, the loaded objects with their load addresses, and for
each thread its id, scheduling state, the object it is blocked on, pc, lr and
sp, each code address as object+offset. With --stack, also the words on each
thread's stack that point into a loaded object: a heuristic, since stale
return addresses stay on the stack after their call returned.

The note layout below was read from cores of the car (2026-11-17), not from
QNX headers:
  type 7  process info (pid first)
  type 8  thread status (debug_thread_t): pid, tid at 0/4, pc at 0x10, sp at
          0x18, state byte at 0x3b, blocked-on object (mutex, condvar or
          semaphore address) at 0x84
  type 9  general registers: r0-r15, cpsr
  type 10 floating point registers
  type 11 link map: r_debug at 0x20 (r_ldbase, libc, at 0x30), then 0x20-byte
          link_map entries from 0x40: l_addr, name, l_ld, l_next, l_prev, -, -,
          folder; name and folder are offsets from the string table base, one
          byte before "EXE".
Only what the dumper wrote is readable: with the car's `dumper -m`, the stacks
are there but most library data is not.

Usage: qnx_core.py CORE [--stack [BYTES]] [--threads 1,24]
"""
import argparse
import struct

STATES = ['DEAD', 'RUNNING', 'READY', 'STOPPED', 'SEND', 'RECEIVE', 'REPLY', 'STACK',
          'WAITTHREAD', 'WAITPAGE', 'SIGSUSPEND', 'SIGWAITINFO', 'NANOSLEEP', 'MUTEX',
          'CONDVAR', 'JOIN', 'INTR', 'SEM', 'WAITCTX', 'NET_SEND', 'NET_REPLY']


class Core:
    def __init__(self, data):
        if data[:4] != b'\x7fELF' or struct.unpack_from('<H', data, 16)[0] != 4:
            raise ValueError('not an ELF core file')
        self.data = data
        self.pid = None
        self.objects = []  # (base, path), sorted
        self.threads = []  # (tid, status bytes, registers)
        self.segments = []  # (start, end, file offset) of dumped memory
        self._read()

    def _program_headers(self):
        phoff, = struct.unpack_from('<I', self.data, 28)
        phentsize, phnum = struct.unpack_from('<HH', self.data, 42)
        for i in range(phnum):
            yield struct.unpack_from('<IIIII', self.data, phoff + i * phentsize)

    def _notes(self, offset, size):
        pos, end = offset, offset + size
        while pos + 12 <= end:
            namesz, descsz, ntype = struct.unpack_from('<III', self.data, pos)
            pos += 12 + ((namesz + 3) & ~3)
            yield ntype, self.data[pos:pos + descsz]
            pos += (descsz + 3) & ~3

    def _read(self):
        status = None
        for p_type, p_off, p_vaddr, _, p_filesz in self._program_headers():
            if p_type == 1 and p_filesz:
                self.segments.append((p_vaddr, p_vaddr + p_filesz, p_off))
            if p_type != 4:
                continue
            for ntype, desc in self._notes(p_off, p_filesz):
                if ntype == 7:
                    self.pid, = struct.unpack_from('<I', desc, 0)
                elif ntype == 8:
                    status = desc
                elif ntype == 9 and status is not None:
                    tid, = struct.unpack_from('<I', status, 4)
                    self.threads.append((tid, status, struct.unpack_from('<17I', desc)))
                    status = None
                elif ntype == 11:
                    self.objects = sorted(self._link_map(desc))

    @staticmethod
    def _link_map(desc):
        base = desc.index(b'EXE\0') - 1

        def text(off):
            return desc[base + off:desc.index(b'\0', base + off)].decode('latin-1')

        out = [(struct.unpack_from('<I', desc, 0x30)[0], 'libc.so.3 (ldqnx)')]
        at = 0x40
        while at + 0x20 <= base:
            w = struct.unpack_from('<8I', desc, at)
            if not w[1] or base + w[1] >= len(desc):
                break
            name = text(w[1])
            if name == 'EXE':
                name = text(w[7]) if w[7] else 'executable'
            elif not name.startswith('/') and w[7]:
                folder = text(w[7])  # a folder, or for libc the file itself
                name = folder + name if folder.endswith('/') else folder
            out.append((w[0], name))
            at += 0x20
        return out

    def where(self, value):
        """object+offset for an address, taking each object to end where the next one
        starts (an upper bound: the real end is not in the core)."""
        for i, (start, name) in enumerate(self.objects):
            end = self.objects[i + 1][0] if i + 1 < len(self.objects) else start + 0x400000
            if start <= value < end:
                return '%s+0x%x' % (name.rsplit('/', 1)[-1], value - start)
        return None

    def word(self, address):
        for start, end, offset in self.segments:
            if start <= address <= end - 4:
                return struct.unpack_from('<I', self.data, offset + address - start)[0]
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('core')
    parser.add_argument('--stack', nargs='?', const=2048, type=int, metavar='BYTES',
                        help='scan this many bytes of each stack (default 2048)')
    parser.add_argument('--threads', help='comma-separated thread ids')
    args = parser.parse_args()
    core = Core(open(args.core, 'rb').read())
    wanted = set(int(t) for t in args.threads.split(',')) if args.threads else None
    print('pid %s' % core.pid)
    for start, name in core.objects:
        print('  %08x %s' % (start, name))
    for tid, status, regs in core.threads:
        if wanted and tid not in wanted:
            continue
        state = status[0x3b] if len(status) > 0x3b else 255
        name = STATES[state] if state < len(STATES) else str(state)
        blocked, = struct.unpack_from('<I', status, 0x84) if len(status) >= 0x88 else (0,)
        on = ' on %08x' % blocked if name in ('MUTEX', 'CONDVAR', 'SEM') else ''
        print('tid %3d %-11s%s pc %s lr %s sp %08x' % (
            tid, name, on, core.where(regs[15]) or '%08x' % regs[15],
            core.where(regs[14]) or '%08x' % regs[14], regs[13]))
        if args.stack is None:
            continue
        for address in range(regs[13], regs[13] + args.stack, 4):
            value = core.word(address)
            if value is None:
                print('    (stack not in the core from sp+0x%x)' % (address - regs[13]))
                break
            place = core.where(value) if value >= 0x100000 else None
            if place:
                print('    sp+0x%03x %08x %s' % (address - regs[13], value, place))


if __name__ == '__main__':
    main()

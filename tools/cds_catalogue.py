#!/usr/bin/env python3
"""Extract CDS property registrations and JSON formats from libhmiremoting.so.

Reads an ARM ELF only; never runs vendor code. Requires the same tools as
most_catalogue.py. CDS reads ProcUnits, NOT MOST payloads. Unknown formats
and all MOST mappings stay unknown; this output cannot generate MOST decoders.
Review output before retaining it as interoperability facts.
"""
import argparse
import csv
import os
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from most_catalogue import Image, Val


def cstring(img, address):
    offset = img.off(address)
    if offset is None:
        return None
    end = img.data.find(b"\0", offset, offset + 4096)
    if end < 0:
        return None
    try:
        return img.data[offset:end].decode("ascii")
    except UnicodeDecodeError:
        return None


def catalogue(img):
    symbols = {name: address for (address, _, _), name in zip(img.syms, img.demangled)}
    vtables = {}
    for name, address in symbols.items():
        match = re.fullmatch(r"vtable for TPC<CCdsProperty(\w+)>", name)
        if match:
            vtables[address + 8] = match[1]
    allocator = symbols.get("operator new(unsigned int)")
    string_ctor = symbols.get("THmiString<char>::THmiString(char const*)")
    factories = {address for name, address in symbols.items()
                 if name == "CCdsPropertyFactory::CCdsPropertyFactory()"}
    if not allocator or not string_ctor or not factories or not vtables:
        raise ValueError("unsupported ELF: CDS factory/allocator/string/vtables missing")
    objects, pending = {}, [None]

    def trace(kind, pc, target, value):
        if kind == "call":
            arg = value.get(1)
            if target == string_ctor and arg and arg.base is None:
                text = cstring(img, arg.k)
                pending[0] = text if text and re.fullmatch(r"[a-z]\w*\.\w+(?:\.\w+)*", text) else None
            if target == allocator:
                return Val("heap:%x" % pc, 0)
        elif target.base and target.base.startswith("heap:") and value and value.base is None:
            obj = objects.setdefault(target.base, {})
            obj[target.k] = value.k
            if target.k == 0 and value.k in vtables and "name" not in obj:
                obj["name"] = pending[0]
                obj["site"] = pc
        return None

    for factory in factories:
        img.emulate(factory, limit=100000, trace=trace)
    rows = []
    for obj in objects.values():
        cls = vtables.get(obj.get(0))
        if not cls or not obj.get("name"):
            continue
        formats, methods = set(), []
        for method in ("onStatus()", "getFormatedValue()"):
            name = "CCdsProperty%s::%s" % (cls, method)
            address = symbols.get(name)
            if address is None:
                continue
            methods.append("%s@0x%x" % (method, address))
            for _, target, _, arg, _ in img.emulate(address)[0]:
                if arg and arg.base is None:
                    text = cstring(img, arg.k)
                    if text and text.startswith("{") and "%" in text:
                        formats.add(text)
        rows.append((obj["name"], cls, obj.get(8, "unknown"), obj.get(12, "unknown"),
                     " | ".join(sorted(formats)) or "unknown", "unknown", "unknown", "unknown",
                     "factory@0x%x;%s" % (obj["site"], ";".join(methods))))
    if not rows:
        raise ValueError("unsupported CDS factory instruction pattern (no registrations)")
    return sorted(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("elf")
    args = parser.parse_args()
    try:
        img = Image(args.elf, os.environ.get("NM", "arm-none-eabi-nm"),
                    os.environ.get("CXXFILT", "arm-none-eabi-c++filt"))
        rows = catalogue(img)
    except ValueError as error:
        parser.error(str(error))
    writer = csv.writer(sys.stdout, delimiter="\t", lineterminator="\n")
    writer.writerow(("property", "class", "property_id", "procunit_id", "json_format",
                     "fblock", "instance", "function", "evidence"))
    writer.writerows(rows)
    print("cds_catalogue: %d registrations; MOST mappings unknown" % len(rows), file=sys.stderr)


if __name__ == "__main__":
    main()

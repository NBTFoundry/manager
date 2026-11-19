# SPDX-License-Identifier: MPL-2.0
"""Bridge wire validation shared by signing tools; no signing keys here."""
import re

VERSION = re.compile(r"([1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\Z")
NAME = re.compile(r"[a-z][a-z0-9-]{0,23}\Z")
CURRENT = ("1.5|show,status,live-settings,most,gamepad,controller,files",)  # matches nbt_bridge_current
# One rule of a MOST policy (src/model/most_policy.cpp, docs/most-bridge.md).
MOST_RULE = re.compile(r"(?:rx:\*\.\*\.\*|(?:rx|tx):[0-9A-F]{2}\.(?:\*|[0-9A-F]{2})\.(?:\*|[0-9A-F]{3})"
                       r"|ctl:[0-9A-F]{2}\.[0-9A-F]{2}|sub:[0-9A-F]{2}\.[0-9A-F]{2}\.[0-9A-F]{3})\Z")
MOST_MAX, MOST_RULES, MOST_CONTROLS = 640, 48, 16
# A files policy (src/model/files_policy.cpp, docs/files-bridge.md).
FILES_EXTENSION = re.compile(r"[a-z0-9]{1,8}\Z")
FILES_NUMBER = re.compile(r"[1-9][0-9]{0,4}\Z")
FILES_MAX, FILES_EXTENSIONS, FILES_FILE_MIB, FILES_QUOTA_MIB = 160, 16, 4096, 65536


def version(text):
    match = VERSION.fullmatch(text)
    if not match or any(int(x) > 65535 for x in match.groups()):
        raise ValueError("bridge version must be canonical major.minor (1..65535, 0..65535)")
    return tuple(int(x) for x in match.groups())


def caps(text):
    names = text.split(",") if text else []
    if len(text) > 255 or len(set(names)) != len(names) or not all(NAME.fullmatch(n) for n in names):
        raise ValueError("invalid or duplicate bridge capability")
    return set(names)


def most_policy(text):
    """Validates a MOST policy as the C parser does; returns it unchanged."""
    rules = text.split(",")
    if (not text or len(text) > MOST_MAX or len(rules) > MOST_RULES or len(set(rules)) != len(rules) or
            not all(MOST_RULE.fullmatch(r) for r in rules) or
            sum(r.startswith("ctl:") for r in rules) > MOST_CONTROLS or
            sum(r.startswith("sub:") for r in rules) > 32 or
            any(r.startswith("sub:01.") for r in rules) or
            any(r.startswith(("tx:", "sub:")) and r.rsplit(".", 1)[1] in ("000", "001", "002") for r in rules)):
        raise ValueError("invalid MOST policy (see docs/most-bridge.md)")
    receivers = [r[3:].split(".") for r in rules if r.startswith("rx:")]
    for rule in rules:
        if rule.startswith("sub:"):
            fields = rule[4:].split(".")
            if not any(all(a == "*" or a == b for a, b in zip(rx, fields)) for rx in receivers):
                raise ValueError("MOST subscription needs rx coverage")
    return text


def files_policy(text):
    """Validates a files policy as the C parser does; returns it unchanged."""
    parts = text.split("|")
    if len(text) > FILES_MAX or len(parts) != 3:
        raise ValueError("invalid files policy: extensions|largest file MiB|quota MiB")
    extensions = parts[0].split(",")
    if (not 1 <= len(extensions) <= FILES_EXTENSIONS or len(set(extensions)) != len(extensions) or
            not all(FILES_EXTENSION.fullmatch(e) for e in extensions) or
            not all(FILES_NUMBER.fullmatch(n) for n in parts[1:])):
        raise ValueError("invalid files policy (see docs/files-bridge.md)")
    largest, quota = int(parts[1]), int(parts[2])
    if largest > FILES_FILE_MIB or quota > FILES_QUOTA_MIB or quota < largest:
        raise ValueError("files policy limits: largest file 1-4096 MiB, quota 1-65536 MiB and at least the largest file")
    return text


def requirement(ver, uses, optional, most="", files=""):
    if ver is None:
        if uses or optional or most or files:
            raise ValueError("bridge capabilities require --bridge")
        return ""
    major, minor = version(ver)
    if major == 1 and minor < 2 and any(r.startswith("sub:") for r in most.split(",")):
        raise ValueError("MOST subscriptions require bridge 1.2")
    required, wanted = caps(uses), caps(optional)
    if required & wanted:
        raise ValueError("required and optional bridge capabilities overlap")
    if ("most" in required | wanted) != bool(most):
        raise ValueError("a MOST policy (--bridge-most) is required exactly when most is declared")
    if ("files" in required | wanted) != bool(files):
        raise ValueError("a files policy (--bridge-files) is required exactly when files is declared")
    if files and major == 1 and minor < 5:
        raise ValueError("files requires bridge 1.5")
    text = f"bridge={ver}\nbridge-uses={uses}\nbridge-optional={optional}\n"
    text += f"bridge-most={most_policy(most)}\n" if most else ""
    return text + (f"bridge-files={files_policy(files)}\n" if files else "")


def provider(values):
    previous = 0
    if len(values) > 2:
        raise ValueError("at most two bridge majors")
    for text in values:
        ver, sep, names = text.partition("|")
        if not sep:
            raise ValueError("manager bridge must be major.minor|capabilities")
        major, _ = version(ver)
        if major <= previous:
            raise ValueError("bridge majors must be unique and increasing")
        caps(names)
        previous = major
    return "".join(f"bridge={v}\n" for v in values)

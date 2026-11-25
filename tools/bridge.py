# SPDX-License-Identifier: MPL-2.0
"""Bridge wire validation shared by signing tools; no signing keys here."""
import re

VERSION = re.compile(r"([1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\Z")
NAME = re.compile(r"[a-z][a-z0-9-]{0,23}\Z")
CURRENT = ("1.9|show,status,live-settings,most,gamepad,controller,files,hold,car-data,net,runner,diag,service",)  # nbt_bridge_current
# One rule of a MOST policy (src/model/most_policy.cpp, docs/most-bridge.md).
MOST_RULE = re.compile(r"(?:rx:\*\.\*\.\*|(?:rx|tx):[0-9A-F]{2}\.(?:\*|[0-9A-F]{2})\.(?:\*|[0-9A-F]{3})"
                       r"|ctl:[0-9A-F]{2}\.[0-9A-F]{2}|sub:[0-9A-F]{2}\.[0-9A-F]{2}\.[0-9A-F]{3})\Z")
MOST_MAX, MOST_RULES, MOST_CONTROLS = 640, 48, 16
# A files policy (src/model/files_policy.cpp, docs/files-bridge.md).
FILES_EXTENSION = re.compile(r"[a-z0-9]{1,8}\Z")
FILES_NUMBER = re.compile(r"[1-9][0-9]{0,4}\Z")
FILES_MAX, FILES_EXTENSIONS, FILES_FILE_MIB, FILES_QUOTA_MIB = 160, 16, 4096, 65536
# A car-data policy (src/model/cds_policy.cpp) and a network policy (src/model/net_policy.cpp).
CDS_NAME = re.compile(r"[A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)+\Z")
CDS_MAX, CDS_NAMES, CDS_NAME_MAX = 255, 16, 64
NET_RULE = re.compile(r"tcp:([1-9][0-9]{0,4})@((?:uap0|wfd0)(?:\+(?:uap0|wfd0))?)\Z")
NET_MAX, NET_RULES = 96, 4
# A diagnostics policy (src/model/diag_policy.cpp, docs/diag-bridge.md).
DIAG_RULE = re.compile(r"([0-9a-f]{2}):(read(?:\+codes)?(?:\+clear)?|codes(?:\+clear)?|clear)\Z")
DIAG_MAX, DIAG_RULES = 160, 8


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
    marks = None
    if len(parts) == 4 and parts[3].startswith("folders="):  # docs/files-bridge.md, game folders
        marks = parts[3][len("folders="):].split(",")
        parts = parts[:3]
    if len(text) > FILES_MAX or len(parts) != 3:
        raise ValueError("invalid files policy: extensions|largest file MiB|quota MiB[|folders=ext,...]")
    extensions = parts[0].split(",")
    if (not 1 <= len(extensions) <= FILES_EXTENSIONS or len(set(extensions)) != len(extensions) or
            not all(FILES_EXTENSION.fullmatch(e) for e in extensions) or
            not all(FILES_NUMBER.fullmatch(n) for n in parts[1:])):
        raise ValueError("invalid files policy (see docs/files-bridge.md)")
    if marks is not None and (not marks or len(set(marks)) != len(marks) or
                              not all(m in extensions for m in marks)):
        raise ValueError("files policy: folders= names declared extensions, each once")
    largest, quota = int(parts[1]), int(parts[2])
    if largest > FILES_FILE_MIB or quota > FILES_QUOTA_MIB or quota < largest:
        raise ValueError("files policy limits: largest file 1-4096 MiB, quota 1-65536 MiB and at least the largest file")
    return text


def cds_policy(text):
    """Validates a car-data policy as the C parser does; returns it unchanged."""
    names = text.split(",")
    if (not text or len(text) > CDS_MAX or len(names) > CDS_NAMES or len(set(names)) != len(names) or
            not all(3 <= len(n) <= CDS_NAME_MAX and CDS_NAME.fullmatch(n) for n in names)):
        raise ValueError("invalid car-data policy: 1-16 unique dotted names of 3-64 [A-Za-z0-9_.]")
    return text


def net_policy(text):
    """Validates a network policy as the C parser does; returns it unchanged."""
    rules = [NET_RULE.fullmatch(r) for r in text.split(",")]
    ports = [int(m.group(1)) for m in rules if m]
    if (not text or len(text) > NET_MAX or len(rules) > NET_RULES or not all(rules) or
            any(p > 65535 for p in ports) or len(set(ports)) != len(ports) or
            any(m.group(2) in ("uap0+uap0", "wfd0+wfd0") for m in rules)):
        raise ValueError("invalid network policy: tcp:<port>@uap0|wfd0[+...], at most 4 distinct ports")
    return text


def diag_policy(text):
    """Validates a diagnostics policy as the C parser does; returns it unchanged."""
    rules = [DIAG_RULE.fullmatch(r) for r in text.split(",")]
    ecus = [int(m.group(1), 16) for m in rules if m]
    if (not text or len(text) > DIAG_MAX or len(rules) > DIAG_RULES or not all(rules) or
            ecus != sorted(set(ecus))):
        raise ValueError("invalid diagnostics policy: <ecu hex>:read[+codes][+clear],... "
                         "ECUs ascending, at most 8")
    return text


def requirement(ver, uses, optional, most="", files="", cds="", net="", diag=""):
    if ver is None:
        if uses or optional or most or files or cds or net or diag:
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
    if ("car-data" in required | wanted) != bool(cds):
        raise ValueError("a car-data policy (--bridge-cds) is required exactly when car-data is declared")
    if ("net" in required | wanted) != bool(net):
        raise ValueError("a network policy (--bridge-net) is required exactly when net is declared")
    if (cds or net) and major == 1 and minor < 6:
        raise ValueError("car-data and net require bridge 1.6")
    if ("diag" in required | wanted) != bool(diag):
        raise ValueError("a diagnostics policy (--bridge-diag) is required exactly when diag is declared")
    if diag and major == 1 and minor < 8:
        raise ValueError("diag requires bridge 1.8")
    if "service" in required | wanted and ((major == 1 and minor < 9) or "show" not in required | wanted):
        raise ValueError("service requires bridge 1.9 and show (docs/service-bridge.md)")
    # A payload built without the QNX SDP cannot run without the runner.
    if "runner" in wanted:
        raise ValueError("runner is required (--bridge-uses), never optional")
    if "runner" in required and major == 1 and minor < 7:
        raise ValueError("runner requires bridge 1.7")
    text = f"bridge={ver}\nbridge-uses={uses}\nbridge-optional={optional}\n"
    text += f"bridge-most={most_policy(most)}\n" if most else ""
    text += f"bridge-files={files_policy(files)}\n" if files else ""
    text += f"bridge-cds={cds_policy(cds)}\n" if cds else ""
    text += f"bridge-net={net_policy(net)}\n" if net else ""
    return text + (f"bridge-diag={diag_policy(diag)}\n" if diag else "")


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

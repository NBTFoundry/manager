#!/usr/bin/python3
# SPDX-License-Identifier: MPL-2.0
"""A stand-in for /opt/sys/bin/LayerManagerControl in host tests.

State (JSON at NBT_FAKE_LM_STATE): the render order of layer 0x80000000,
surface visibility, the surfaces with a window (`windows`, default the
manager's and the application's) and after how many `get surface` queries
they exist. Each call is logged to NBT_FAKE_LM_LOG as "<ppid> <args>".
NBT_FAKE_LM_HANG_S makes calls wait that long first (only those whose
arguments start with NBT_FAKE_LM_HANG_ON, if set), as LayerManager answered
nothing for 28 s after a boot on the car (dev79).
The output formats are those NBT-AAD parses on the car."""
import json
import os
import sys
import time

state_path = os.environ["NBT_FAKE_LM_STATE"]
with open(state_path) as handle:
    state = json.load(handle)
args = sys.argv[1:]
with open(os.environ["NBT_FAKE_LM_LOG"], "a") as log:
    log.write("%d %s\n" % (os.getppid(), " ".join(args)))
if os.environ.get("NBT_FAKE_LM_ENV_LOG"):
    with open(os.environ["NBT_FAKE_LM_ENV_LOG"], "a") as env_log:
        env_log.write("LD_PRELOAD=%s\n" % ("present" if "LD_PRELOAD" in os.environ else "absent"))
if " ".join(args).startswith(os.environ.get("NBT_FAKE_LM_HANG_ON", "")):
    time.sleep(float(os.environ.get("NBT_FAKE_LM_HANG_S", "0")))
print("unknown symbol: kdMain referenced from libKD.so.1", file=sys.stderr)
if args[:2] == ["get", "surface"]:
    state["queries"] = state.get("queries", 0) + 1
    bound = (int(args[2]) in state.get("windows", [1290, 1291])
             and state["queries"] >= state.get("bound_after", 1))
    print("surface %s:\n    - native surface:       %s" % (args[2], 4242 if bound else 4294967295))
elif args == ["get", "screen", "0"]:
    print("screen 0 (0x0)\n---------------------------------------\n"
          "    - resolution:           x=1440, y=540")
elif args == ["get", "layer", "0x80000000"]:
    order = "".join(" %d(0x%x)," % (surface, surface) for surface in state["order"])
    print("layer 2147483648 (0x80000000)\n    - surface render order:%s" % order)
elif args[:5] == ["set", "layer", "0x80000000", "render", "order"]:
    state["order"] = [int(item, 16) for item in args[5].split(",")]
elif args[:2] == ["set", "surface"] and args[3] == "visibility":
    state["visible"][args[2]] = int(args[4])
# Atomic, so a test reading the state never sees it half written.
with open(state_path + ".next", "w") as handle:
    json.dump(state, handle)
os.replace(state_path + ".next", state_path)

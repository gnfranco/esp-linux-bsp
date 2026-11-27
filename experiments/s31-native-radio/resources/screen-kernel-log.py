#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Screen captured kernel logs. No match is not a qualification PASS."""
import argparse
import pathlib
import re
import sys

ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
PREFIX = r"^\s*(?:~ #\s*)?(?:\[\s*\d+(?:\.\d+)?\]\s*)?"
RULES = {
    "rcu_stall": re.compile(PREFIX + r"rcu:.*(?:self-detected stall|detected stalls?|kthread starved|timer wakeup didn.t happen|RCU CPU stall)", re.I),
    "kernel_fault": re.compile(PREFIX + r"(?:BUG:|Oops\b|Kernel panic|Unable to handle kernel|WARNING: CPU:|watchdog:.*lockup|soft lockup|list_(?:add|del) corruption|list_add double add)", re.I),
    "hung_task": re.compile(PREFIX + r"INFO: task .* blocked for more than", re.I),
    "usb_disconnect": re.compile(PREFIX + r"usb .*?USB disconnect", re.I),
    "usb_timeout": re.compile(PREFIX + r"usb .*?(?:error -110\b|timed out|timeout\b)", re.I),
}

def screen(data):
    data = ANSI.sub("", data.replace("\x00", "").replace("\r", ""))
    return [(kind, line) for line in data.splitlines()
            for kind, rx in RULES.items() if rx.search(line)]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    faults = 0
    for p in args.logs:
        matches = screen(p.read_text(errors="replace"))
        for kind, line in matches:
            print(f"{p.name}: {kind}: {line}")
            faults += 1
        print(f"{p.name}: {len(matches)} matched records")
    print("KERNEL_LOG_ALERTS" if faults else "KERNEL_LOG_NO_MATCHES")
    return int(bool(faults))

if __name__ == "__main__":
    sys.exit(main())

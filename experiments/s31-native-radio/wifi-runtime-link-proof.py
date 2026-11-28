#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reproduce data/math/pure-helper relocatable link proofs; never execute proprietary radio archives."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess as sp
import shutil

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("sources", type=Path, help="private hash-pinned IDF source root")
ap.add_argument("wifi_init", type=Path)
ap.add_argument("ftm_closure", type=Path)
ap.add_argument("cross_prefix")
ap.add_argument("output", type=Path)
a = ap.parse_args()
src, wifi_init, closure = (p.resolve(strict=True) for p in (a.sources, a.wifi_init, a.ftm_closure))
prefix = a.cross_prefix
def executable(name):
    value = shutil.which(name)
    if value is None:
        raise RuntimeError(f"tool not found: {name}")
    return os.path.abspath(value)
tools = {prefix+n: executable(prefix+n) for n in ("gcc", "ld", "ar", "nm", "readelf")}
tools["cc"] = executable("cc")
for n in ("git", "sha256sum"):
    tools[n] = executable(n)
test_source = Path(__file__).resolve().with_name("test-wifi-pure-helpers.c")

def run(*args, capture=False, **kw):
    return sp.run([tools.get(str(args[0]), str(args[0])), *[str(x) for x in args[1:]]], check=True, text=True,
                  stdout=sp.PIPE if capture else None, **kw).stdout

def sha(path, expected=None):
    value = hashlib.sha256(path.read_bytes()).hexdigest()
    if expected is not None and value != expected:
        raise RuntimeError(f"unexpected SHA256 for {path}")
    return value

def git_source(path, expected):
    if run("git", "hash-object", path, capture=True).strip() != expected:
        raise RuntimeError(f"unexpected Git blob: {path}")
    return path.read_text()

wifi = src / "components/esp_wifi"
event = src / "components/esp_event/include"
common = src / "components/wpa_supplicant/src/utils"
reg = git_source(wifi / "regulatory/esp_wifi_regulatory.c", "d6028adb14fa43c90724c726d34db7fbde2fe4ea")
git_source(wifi / "include/esp_wifi_types_generic.h", "6be8440d2311835341ebf7e1244fcee9a249a362")
git_source(event / "esp_event_base.h", "2dbdd71df89ab9b8422795d8bc680f3eec44ba0f")
caps = git_source(src / "components/soc/esp32s31/include/soc/soc_caps.h", "53e2aec61a3cc50356e52696e5dca0dc40c00f71")
c = git_source(common / "common.c", "ef36edc7dca3815a2b0b89a04c74493f22a9739c")
h = git_source(common / "common.h", "4fa4e11798a165ec6b79c200f00d58b2213b20b6")
w = git_source(wifi_init, "e50a7f4a19c42fdb404902fa2f7cedf39c34e043")
sha(closure, "817f175e6a5d58ab2873a720d557944d58651508e5ab09ab85c0885c5c19e7f1")
if re.search(r"^\s*#define\s+SOC_WIFI_SUPPORT_5G\b", caps, re.M):
    raise RuntimeError("unexpected 5 GHz capability")
for name in ("SOC_WIFI_HE_SUPPORT", "SOC_WIFI_SUPPORTED", "SOC_WIFI_NAN_SUPPORT"):
    if not re.search(r"^\s*#define\s+"+name+r"\s+\(?1\)?\b", caps, re.M):
        raise RuntimeError("unexpected capability "+name)

os.umask(0o077)
out = a.output.resolve()
out.mkdir()
os.chdir(out)
os.environ["LC_ALL"] = "C"
cc = prefix+"gcc"
isa = ["-march=rv32imafc", "-mabi=ilp32f"]
common_flags = isa + ["-ffreestanding", "-fdata-sections", "-Wall", "-Wextra", "-Werror"]
includes = ["-I.", "-I"+str(wifi/"include"), "-I"+str(event)]
# Narrow data-only translation unit: remove exactly one unused public API include;
# retain the entire official table body and generic type header.
if reg.count('#include "esp_wifi.h"\n') != 1:
    raise RuntimeError("unexpected regulatory include")
Path("regulatory-data.c").write_text(reg.replace('#include "esp_wifi.h"\n', "", 1))
Path("sdkconfig.h").write_text("#define CONFIG_IDF_TARGET_ESP32S31 1\n#define CONFIG_SOC_WIFI_SUPPORTED 1\n#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n#define CONFIG_SOC_WIFI_NAN_SUPPORT 1\n")
run(cc, *common_flags, *includes, "-c", "regulatory-data.c", "-o", "regulatory.o")

def und(obj):
    return {line.split()[-1] for line in run(prefix+"nm", "-u", obj, capture=True).splitlines() if line.split()}

def attributes(obj):
    text = run(prefix+"readelf", "-h", "-A", obj, capture=True)
    if "single-float ABI" not in text or not re.search(r'Tag_RISCV_arch: "rv32', text):
        raise RuntimeError("not RV32 ILP32F")
    arch = re.search(r'Tag_RISCV_arch: "([^"]+)"', text).group(1)
    if arch != "rv32i2p1_m2p0_a2p1_f2p2_c2p0_zicsr2p0_zmmul1p0_zca1p0_zcf1p0":
        raise RuntimeError("unexpected ISA attribute: "+arch)
    return arch

def stage(label, original, extras, expected_before, expected_after, expected_resolved=None):
    control, candidate = label+"-control.o", label+"-closure.o"
    run(prefix+"ld", "-m", "elf32lriscv", "-r", original, "-o", control)
    run(prefix+"ld", "-m", "elf32lriscv", "-r", original, *extras,
        "-Map="+label+".map", "-o", candidate)
    before, after = und(control), und(candidate)
    removed, introduced = before-after, after-before
    for name, items in (("before", before), ("after", after), ("resolved", removed), ("introduced", introduced)):
        Path(label+"-"+name+".txt").write_text("".join(x+"\n" for x in sorted(items)))
    if len(before)!=expected_before or len(after)!=expected_after or introduced:
        raise RuntimeError(f"{label}: unexpected link delta")
    if expected_resolved is not None and removed != expected_resolved:
        raise RuntimeError(f"{label}: unexpected resolved set")
    attributes(candidate)
    print(label, len(before), "->", len(after), "resolved", len(removed), flush=True)
    return candidate

attributes("regulatory.o")
if und("regulatory.o"):
    raise RuntimeError("regulatory object has undefined symbols")
reg_obj = stage("regulatory", closure, ["regulatory.o"], 193, 191,
                {"regdomain_table", "regulatory_data"})

# Use the actual ILP32F runtime archives; no address-based ROM substitutions.
libgcc = Path(run(cc, *isa, "-print-libgcc-file-name", capture=True).strip()).resolve(strict=True)
libc = Path(run(cc, *isa, "-print-file-name=libc.a", capture=True).strip()).resolve(strict=True)
sha(libgcc, "3f02b11387714d45babfce91c4f77d16ff0f6fbb082b9b1cd45f9d398c6c6835")
sha(libc, "3dda872fd193788b21329bee0aeb7a34af52e0e1f6dc5218497cf61313f20f35")
with open("floor.o", "wb") as f:
    sp.run([tools[prefix+"ar"], "p", str(libc), "floor.o"], check=True, stdout=f)
sha(Path("floor.o"), "77cd81240556895eedc567b7ec7e645ec1cffe52c632f6a85c48e9496e4110cd")
attributes("floor.o")
math_names = set("__adddf3 __ashldi3 __bswapsi2 __clzsi2 __ctzsi2 __divdf3 __divdi3 __extendsfdf2 __ffssi2 __fixdfsi __fixunsdfdi __floatsidf __floatundidf __floatundisf __floatunsidf __lshrdi3 __moddi3 __muldf3 __popcountdi2 __popcountsi2 __subdf3 __truncdfsf2 __udivdi3 __umoddi3 floor".split())
math_obj = stage("math", reg_obj, ["floor.o", libgcc], 191, 166, math_names)
# Check actual selected archive members from the map, then the definitions
# supplied by those members. Do not infer ROM ABI compatibility from names.
selected = set(re.findall(re.escape(str(libgcc))+r"\(([^()]+)\)", Path("math.map").read_text()))
providers = {"floor": "floor.o"}
if "floor" not in run(prefix+"nm", "-g", "--defined-only", "floor.o", capture=True):
    raise RuntimeError("floor provider missing")
for line in run(prefix+"nm", "-A", "-g", "--defined-only", libgcc, capture=True, stderr=sp.DEVNULL).splitlines():
    f=line.split()
    if len(f)<3 or f[-1] not in math_names-{"floor"}:
        continue
    m=re.match(re.escape(str(libgcc))+r":([^:]+):",line)
    if m and m[1] in selected:
        if f[-1] in providers:
            raise RuntimeError("ambiguous math provider "+f[-1])
        providers[f[-1]]=m[1]
if set(providers)!=math_names:
    raise RuntimeError("incomplete selected math provider inventory")
Path("math-providers.json").write_text(json.dumps(providers, indent=2, sort_keys=True)+"\n")


# Extract the exact three pure WPA helpers from pinned source, preserving its
# copyright/license notice; use the IDF uint8_t typedef, not a guessed ABI.
parts = []
for name in ("hex2num", "hex2byte", "hexstr2bin"):
    match = re.findall(r"(?m)^(?:static )?int "+name+r"\([^\n]*\)\n\{\n[\s\S]*?^\}", c)
    if len(match)!=1:
        raise RuntimeError("unexpected helper source "+name)
    parts.append(match[0])
typedef = re.findall(r"(?m)^typedef uint8_t u8;$", h)
base = re.findall(r"(?m)^ESP_EVENT_DEFINE_BASE\(WIFI_EVENT\);$", w)
if len(typedef)!=1 or len(base)!=1:
    raise RuntimeError("unexpected helper typedef/event base")
Path("helpers.c").write_text(c[:c.index('#include "includes.h"')]+
    "#include <stdint.h>\n#include <stddef.h>\n#include \"esp_event_base.h\"\n"+
    typedef[0]+"\n\n"+"\n\n".join(parts)+"\n\n"+
    "/* IDF wifi_init.c event-base definition, not an event loop. */\n"+base[0]+"\n")
host_flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-I"+str(event)]
run("cc", *host_flags, "-O2", "helpers.c", test_source, "-o", "host-test")
run("./host-test")
run("cc", *host_flags, "-O1", "-g", "-fsanitize=address,undefined",
    "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "helpers.c", test_source, "-o", "host-sanitized")
run("./host-sanitized", env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1"))
run(cc, *common_flags, "-ffunction-sections", "-I"+str(event), "-c", "helpers.c", "-o", "helpers.o")
run(cc, *isa, "-O2", "-Wall", "-Wextra", "-Werror", "-static", "-I"+str(event),
    "helpers.c", test_source, "-o", "test-rv32")
attributes("test-rv32")
if und("helpers.o"):
    raise RuntimeError("pure helpers have undefined symbols")
final = stage("wifi-helpers", math_obj, ["helpers.o"], 166, 164, {"WIFI_EVENT", "hexstr2bin"})
# All surviving undefined names must have genuine relocations.
rel = run(prefix+"readelf", "-rW", final, capture=True)
Path("relocations.txt").write_text(rel)
u = und(final)
sites = {x: {} for x in sorted(u)}
section = ""
for line in rel.splitlines():
    m = re.match(r"Relocation section '([^']+)'", line)
    if m:
        section=m[1]
        continue
    f=line.split()
    if len(f)>=5 and f[2].startswith("R_RISCV_") and f[4] in sites:
        key=section+" "+f[2]
        sites[f[4]][key]=sites[f[4]].get(key,0)+1
if not all(sites.values()):
    raise RuntimeError("unreferenced undefined symbol")
Path("remaining-relocation-sites.json").write_text(json.dumps(sites, indent=2, sort_keys=True)+"\n")
tool_evidence = {}
for k, path in tools.items():
    tool_evidence[k] = {"path": path, "sha256": sha(Path(path))}
for name in ("crt1.o", "crti.o", "crtn.o", "crtbeginT.o", "crtend.o", "include/stdint.h"):
    path = Path(run(cc, *isa, "-print-file-name="+name, capture=True).strip())
    if path.is_file():
        tool_evidence[name] = {"path": str(path.resolve()), "sha256": sha(path)}
Path("tool-evidence.json").write_text(json.dumps(tool_evidence, indent=2, sort_keys=True)+"\n")
files = sorted(p for p in out.iterdir() if p.is_file())
Path("SHA256SUMS").write_text("".join(sha(p)+"  "+p.name+"\n" for p in files))
run("sha256sum", "-c", "SHA256SUMS")
print("S31_WIFI_RUNTIME_LINK_PROOF_PASS", flush=True)

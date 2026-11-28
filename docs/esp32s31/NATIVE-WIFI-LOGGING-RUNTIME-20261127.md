# Native Wi-Fi logging/runtime closure: Codex implementation

Continuation of NATIVE-WIFI-RUNTIME-CLOSURE-20261127.md.
Two isolated Codex jobs, Sol medium (logging/runtime) and Astra high
(ROM/PHY), bounded to 240 seconds each, both exited 124. Neither
delivered a final report. Sol implemented three actual variadic logging
wrappers and tests. Astra left investigation logs, no source changes.
Do not treat interrupted jobs as complete reviews.

Sol implementation uses vfprintf(stderr, ...) with actual return value
and error reporting for pp_printf, net80211_printf, phy_printf. Linux
thread context ONLY; IRAM diagnostic paths identified, ISR/signal safety
not qualified. No radio execution. Normal and ASan/UBSan tests passed
inside Codex (leak detection disabled due sandbox restriction).
Supervisor independently reproduced with leak detection enabled,
all three wrappers passing long output, mixed arguments, embedded NUL,
formatting failure and /dev/full write failure tests. RV32 ILP32F
standalone test compiled but not executed on board.

Reproduction:
```sh
d="$HOME/deps/s31-research/wifi-first-20261127"
experiments/s31-native-radio/wifi-logging-link-proof.sh "$d/free-osi-link-proof-02/scoped-osi-closure.o" "$HOME/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-" "$d/codex-runtime-next-01/repro-new"
```

Matched ld -r controls, exact removal/addition sets:
168 -> 167 (three log functions resolved, stderr/vfprintf introduced);
167 -> 148 with pinned ILP32F musl libc and libgcc archive group.
19 runtime names resolved, none newly undefined in second step.
All remaining 148 match previously verified ROM maps: 100 in
ROM-reserved SRAM, 48 in ROM aperture. The math/runtime provider
selection must not be interpreted as runtime ABI qualification;
link map retained; no final ELF startup or full process execution.
The sole examined radio free relocation remains radio_osi_free.
Ordinary allocator/backend free resolves to musl. No global
interposition. Exact ISA attribute checked; no D extension.
Marker S31_WIFI_LOGGING_RUNTIME_LINK_PASS.
Supervisor reproduced runtime.o byte-identical to Sol's trial:
SHA-256 1a3a30b511390c1618dbff01cb147ac8e9fc11185230665e7c49240836658fac.
Manifest SHA-256 2ac0cddcc2258e8f9f76d9183dd8d955c9b042d0d85494ae457455403d5aa26d.
Test ELF SHA-256 dcd2fc29c84cffbd277fc0065e3c62363ed49f8da4cdbdd59c2a03d2bd2f27e4.
Private outputs under codex-runtime-next-01/repro-01.

Astra's partial audit found wifi_osi_funcs_register can return success
if g_osi_funcs_p is nonzero before validating new version/magic.
Relocation inventory locates writes from wifi_osi_funcs_register
to g_osi_funcs_p, wifi_init_in_caller_task to g_wifi_global_lock,
and wdev_funcs_init/deinit to pp_wdev_funcs. Need confirm exact
initialization order, lifetime, existing boot state and privilege/
ownership before any registration. Do not blanket-zero ROM SRAM or
map arbitrary hardware. Six additional exact IDF PHY/clock/ROM layout
sources obtained separately, verified against Git blobs and retained in
phy-init-sources/sources.tsv, manifest SHA-256
623d2b0f14fde1ffabd0c67b3a3470072fa1a880231a02aafa68c93c87fec52b.
Astra did not finish executable checker; private raw investigation
log codex-rom-next-01/agent.log. No hardware writes or radio tests
during this step. Next gate: narrow ROM initialization and resource
ownership checker from actual source and relocation evidence, then
verified execution-domain design. Use smaller Codex tasks to avoid
another broad timeout. No upstream submission.

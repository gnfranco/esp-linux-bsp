# Native Wi-Fi: matched relocatable link and FTM definitions

2026-11-27. Link-only experiment, no vendor execution or hardware.
Original selected closure SHA-256
adec29146d41273e904a9f5b420baed911b2243b8d5b3331ce177720a52e5b22.
The original 345 unique nm undefined names are an overcount of
the actual surviving relocatable dependencies: a matched ld -r
control drops 128 local UND names with no relocation references,
including 11 original non-ROM names. Readelf -Ws and -rW confirmed
these were all LOCAL and none appeared in the relocation-name set.
Do not count that normalization as resolved code.

Retrieved pinned IDF v6.1 Git blobs, verified each with git hash-object.
Private source root ~/deps/s31-research/wifi-first-20261127/nonrom-sources/
with sources.tsv. Relevant files:
- components/esp_wifi/src/ftm_load_calibration.c,
  8e937843d78279de4b28e6eabc8933fd4b6d50ab
- components/esp_wifi/include/esp_private/ftm_calibration_data.h,
  be457ad920d675adaefe36f8c8e190a3895065d8
- components/esp_wifi/regulatory/esp_wifi_regulatory.c,
  d6028adb14fa43c90724c726d34db7fbde2fe4ea
- components/esp_wifi/regulatory/reg2fw.py,
  36a4b6bc7b5fe46627382801582df59d583556d7

Compile unchanged FTM source using GCC14.1.1 -march=rv32imafc
-mabi=ilp32f -ffreestanding -fdata-sections, generated sdkconfig
selecting S31 and FTM, official S31 constants from header. Exactly
24 global uint16_t initialized data definitions; no fabricated
constants. Matched relocatable control and FTM link:
217 -> 193 undefined names, exactly 24 definitions removed,
zero introduced. Compare removed set to all FTM global D names.
After normalization and FTM, 180 names match original ROM maps;
13 do not. Remaining non-ROM:
WIFI_EVENT, __fixunsdfdi, floor, free, hexstr2bin, net80211_printf,
phy_printf, pp_printf, putchar, puts, regdomain_table,
regulatory_data, sprintf.
This is NOT a final link or ABI/runtime proof. ROM name matches
still need classification and execution-domain review.

Reproducible script experiments/s31-native-radio/wifi-ftm-link-proof.sh:
d="$HOME/deps/s31-research/wifi-first-20261127"
experiments/s31-native-radio/wifi-ftm-link-proof.sh "$d/nonrom-sources" "$d/repro-01/wifi-full-closure.o" "$HOME/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-" "$d/ftm-link-proof-new"

All 9 output hashes checked. Reproduced byte-identical across
ftm-link-proof-01/02/03. Final run used relative toolchain prefix
after correcting the reviewer finding. FTM object SHA-256
28b3d127be65a27ac557da6451d0e08d7d7519d8828ffe8e5f03418bdd2ce5ac;
control SHA-256
88b12bb9201fe49f5ba9c2aabda2a1f9b7a3cfec57d0aeaed689801f56d509e4;
FTM closure SHA-256
817f175e6a5d58ab2873a720d557944d58651508e5ab09ab85c0885c5c19e7f1.
Marker S31_WIFI_FTM_LINK_PROOF_PASS.

Codex CLI 0.172.0 account rate-limit query succeeded: 76% of
five-hour and 53% of weekly included quota available at start;
no paid credit balance. Two parallel Sol audits of non-ROM and
ROM provenance timed out (status 124 each); logs retained privately,
no completed conclusions. A narrowly scoped Sol read-only review
of FTM script completed (status 0), validated attribution, flagged
relative CROSS_PREFIX handling; corrected and reproduced. Do not
attribute the FTM implementation to the interrupted agents.

Next: compile exact regulatory data source with pinned official
type headers and verified S31 2.4 GHz config; inspect ABI and sizes.
Classify the remaining 180 ROM names after normalization and
resolve 13 non-ROM via real runtime/source definitions, avoiding
fake link success and mixing ROM libc with musl blindly. Continue
combined kernel build/qualification per handoff. No new flash write.

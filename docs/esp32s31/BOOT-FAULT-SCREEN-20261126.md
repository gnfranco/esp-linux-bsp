# S31 Lab v1: boot and anonymous-VMA fault screening (2026-11-26)

## Scope and result

Independent investigation of the failure documented in
[WARM-REBOOT-ANON-VMA-20261126.md](WARM-REBOOT-ANON-VMA-20261126.md).
Board: ESP32-S31 Function-CoreBoard-1, 16 MiB NOR/PSRAM. Original Lab v1
kernel `9b41c9c5df88ed883b05662187e431bb3452e491`, two online harts,
RV32IMAC (no F), read-only CramFS. Kernel artifact SHA-256:
`5fe191d8e189a229f68848791bdb22b851b1b1922ca3f4b0a150030ba0f406ed`.
No image changes or flash writes in this screening.

The plan was 10 software reboots and 5 ROM-assisted RTS resets.
The suite stopped early because the automatic recovery failed:

| Condition | Completed | Passed | Failed |
|---|---:|---:|---:|
| SBI software reboot | 5 | 4 | 1 |
| ROM-assisted RTS recovery after failure (not a planned independent trial) | 1 | 0 | 1 |

Software reboot pass latencies: 8.48, 8.31, 8.52, 8.50 s.
Trial 5 reached Linux but emitted an Oops, timed out at 27.02 s
without a root shell. RTS recovery produced the same Oops and also did
not reach root. A subsequent ROM-assisted reset still failed to reach
the console within the 65 s helper deadline; its raw UART was not
captured, so do not group it with the Oops. The final read-only
`esptool verify-flash` confirmed all five Lab v1 component digests;
the subsequent console check passed the no-F, SMP and read-only
CramFS profile. The matrix was not resumed and no physical power
removal was tested. The board is restored to the known v1 *image*,
not qualified as reliable at boot.

## Exact signatures

The saved `s31-ctucanfd-qa-20261126` build's xipImage is byte-for-byte
equal to the release xipImage. Symbolization against its associated
System.map:

| Field | Software reboot #5 | ROM-assisted recovery |
|---|---|---|
| ROM cause | 0x3 SW_SYS_RESET | 0x1 POWERON |
| Task | esp-example-tar | esp-example-tar |
| CPU/PID | 1/38 | 1/38 |
| EPC | c00af0e0 | c00af0e0 |
| RA | c00af098 | c00af098 |
| s2 | 0x11a | 0x11a |
| Fault VA | 0x11e | 0x11e |
| Cause | 0xd (load page fault) | 0xd |
| Symbol | anon_vma_clone+0xb8 | anon_vma_clone+0xb8 |
| Fault halfwords | 2a03 0049 | 2a03 0049 |

The instruction is `lw s4,4(s2)` (0x00492a03). The common
path includes `__split_vma -> vma_modify -> vma_modify_flags ->
mprotect_fixup -> do_mprotect_pkey`. The matching task, instruction,
register and VA support grouping these two occurrences as one
signature, distinct from the previous null VMA `vma_last_pgoff`
panic. Neither signature establishes the origin of the invalid
pointer; do not infer causal MM, SMP, cache, DMA or PSRAM defects
without further evidence.

## Reset path audit

* OpenSBI fork branch `experiment/s31-system-reboot`, commit
  `0940d8a5834d02b93e2be0e60ef6505767862875`, implements both SBI
  reboot kinds with the same mask ROM software_reset entry
  (0x2f800094). Generic SRST sends HALT IPIs, stops the current hart
  logically, then invokes the platform reset. That is not proof
  that the peer has completed hardware quiescence. The platform
  already has a whole D-cache writeback helper.
* U-Boot SPL `7c053400864fbe0e8479a5ab6cebfa57441e906c` initializes
  the flash/cache mapping and PSRAM again. Its PSRAM mode-register
  diagnostics read the registers before reprogramming. Warm/hard
  raw mode register values differ at entry; this alone is expected
  retained configuration, not a demonstration of corrupt data.
* [Official ESP-IDF v6.1.1 S31 reset implementation](https://github.com/espressif/esp-idf/blob/v6.1.1/components/esp_system/port/soc/esp32s31/system_internal.c)
  coordinates the other core, writes back L1 D-cache for SPIRAM,
  waits for UART and quiesces/resets DMA, and sets clocks for restart.
  Its comments identify prevention of post-restart memory corruption
  as a motivation. Linux and OpenSBI do not necessarily share the
  IDF execution and ownership model. Do not port register writes
  wholesale.
* The exact same new signature occurs after RTS. This reduces the
  case for classifying it as *exclusive* to SBI software reboot.
  The RTS path is not a verified cut of power to the board/PSRAM.

Codex CLI 0.172.0 reviewed the curated source and evidence on
Linvora-dev in read-only mode. It independently recommended exact
symbolization, separate reset conditions with true power removal,
and a single-change cache writeback A/B only after proving the
ROM-call and peer-hart quiescence preconditions. Those preconditions
are not yet demonstrated. No OpenSBI reset change in this experiment.

## Reproducibility and next gates

The screening runner is `experiments/s31-reboot/reboot-screen.py`.
It accesses the UART exclusively, checks the expected baseline,
logs privately, exercises software reboot or read-only flash-id
reset, checks a new boot ID and profile, and stops when recovery
fails. After the first experiment, its classification was improved
to preserve KERNEL_OOPS separately from TIMEOUT; mock-serial tests
cover root, Oops before root, Oops without root, panic and timeout.
This revision is host-tested only. No USB storage mount or write.
Run only with exclusive physical-bench authorization.

    python3 experiments/s31-reboot/reboot-screen.py \
        --esptool /path/to/esptool --port /dev/ttyUSB0 \
        --out /private/new-log-directory --warm 10 --hard 5

Logs remain private on Orion
(`~/source/build/s31-reboot-matrix-20261126/`), not in Git.
On Linvora-dev: bounded Codex review at
`~/deps/s31-research/codex-s31-reset-review.md`.
Keep the private Lab v1 rollback archive verified.

Next test gates, in order:

1. Controlled, confirmed removal and restoration of power with
   identical v1 and no flash changes; report separately from RTS.
2. Baseline comparison with CPU1 offline / single-hart boot only
   if the boot configuration can be changed reproducibly; do not
   conflate online CPU count with harts initialized by firmware.
3. Independent diagnostic kernel using CONFIG_DEBUG_LIST (selects
   LIST_HARDENED; DEBUG_KERNEL is already enabled). It can catch
   some list mutations earlier and may change timing and size;
   preserve the default config and verify XIP slot and RAM budget.
   Build/test this separately from F-only. Do not disable protection
   or modify rmap semantics to make the Oops disappear.
4. Only after establishing reset-path dependence and hart quiescence
   consider a one-change OpenSBI cache-writeback A/B. No raw IDF
   register changes or production claim. No generic MM fix proposed.

The F-only v2 patch `060679ff35be74800f0fcb792296e01e5ff524ee`
addresses partial-copy register-state normalization independently.
Its dedicated Linvora build and hardware requalification are separate
from this stability issue. The v1 stability issue is not evidence that
F-only v2 is correct; neither qualifies a new production image.

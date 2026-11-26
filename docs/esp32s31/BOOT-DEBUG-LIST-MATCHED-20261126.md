# S31 matched DEBUG_LIST boot comparison (2026-11-26)

## Method

Use original Lab v1 (no F, both harts online) versus a kernel built
from the same commit with DEBUG_LIST and its LIST_HARDENED dependency.
Keep the release DTB, SPL, OpenSBI/U-Boot and CramFS unchanged.
The code under test is the same kernel source. Only the diagnostic
config differs. Six warm-reboot trials per image, with three seconds
of settling after validating root-shell profile, same exact kernel
command line, same UART, and new boot ID required. Initial boot
and recovery are recorded separately, not counted as warm trials.

The enhanced `experiments/s31-reboot/reboot-screen.py` explicitly
classifies list integrity warnings, plain Oops, panic and timeout,
can stop early after a list warning, and requires a verified profile
before and after reset. On failure, read-only RTS recovery is
attempted; stop if recovery fails. Seven host mock cases passed
(root, Oops, panic, list warning with root/panic/timeout, timeout).

## Baseline results

All five Lab v1 flash-component digests were verified. Initial
console open reported ROM POWERON and reproduced the previous
`vma_last_pgoff` fault/panic (EPC c00a1de2, VA 0x4, two Oops
in init). This was not a warm-reboot trial. Read-only RTS recovery
passed. All six subsequent controlled 2->2 warm reboots passed
(8.00–8.13 s excluding 3 s settle). UART released.

## Exact release rootfs startup audit

The original CramFS was extracted without modification into a
private host directory using fsck.cramfs; return code zero.
SHA-256 `0e68103aafeefdbdb22202b9a950edc04a6c96734d1e12af4431ebdad2fee086`.

`/etc/inittab` runs mount -a, then
`/usr/libexec/esp-example-target-setup`, then starts the configured
hello-world example, followed by the interactive console. The
shipped setup script's only substantive command activates
the loopback interface. `esp-example-tar` is consistent with the
kernel comm truncation of esp-example-target-setup, not evidence
of an archive-extraction workload. BusyBox provides init, sh and ip.
This is a plausible task-name attribution, not proof of which
instruction or operation triggered the earlier fault.

After baseline boot, the exact shipped setup helper was replayed
64 times in steady state with two harts and loopback already UP.
All passed. This is a targeted control, not equivalent to early
boot concurrency or evidence of a repaired kernel. No flash write
or external-network activity in this replay.

Private logs on Orion:
`~/source/build/s31-debug-list-matched-20261126/`.
The diagnostic build and exact symbols remain on Linvora-dev.

## Complete diagnostic build

Cross-build completed on Linvora-dev with zero captured compiler
warnings. XIP address 0x40400000, size 4,306,700 bytes (12,288 bytes
more than original v1), 3,033,332 bytes spare in the 7 MiB slot.
Exact source commit 9b41c9c5df88ed883b05662187e431bb3452e491,
original pinned GCC 14.1.1, only the two listed config changes.
The vmlinux, System.map, build log, config and DTBs are retained in
`~/deps/s31-research/kernel-v1-debug-list-build/`.

| Artifact | SHA-256 |
|---|---|
| Diagnostic XIP | `2bb91365e1ff8b9e34d63c7e86c78b6485453afb3c7c6ed800ed3ab48b246eec` |
| Diagnostic config | `893c8f22b82278ef2e6942117285db96df5784898286a988c5146aa81d77f11b` |
| Private transport archive | `6bc151d7f483f9aa6471a95a9f171c78c652b4918e9dde608fc20ba09f13d86a` |

Transfer via an unpublished private draft in esp-linux-lab, with
both package and constituent digests checked on Orion. The original
Lab v1 release is not modified. The candidate uses the original
release DTB; kernel slot only is changed for bench testing.

## Diagnostic bench results

Before the diagnostic flash write, all five original flash component
digests were verified again. Only the XIP slot at 0x500000 was
temporarily replaced. Both diagnostic XIP and original DTB verified.
Initial diagnostic boot passed, followed by six controlled 2->2 warm
reboots and the final profile check.

| Trial | Result | Elapsed seconds (excluding settle) |
|---|---|---:|
| 1 | PASS | 8.18 |
| 2 | PASS | 8.44 |
| 3 | PASS | 8.25 |
| 4 | PASS | 8.27 |
| 5 | PASS | 8.22 |
| 6 | PASS | 8.25 |

The original kernel passed all six matched warm trials (8.00–8.13 s)
as well. No list integrity warning, Oops, or panic was found in the
seven captured diagnostic UART logs (initial plus six warm boots).
The baseline's separate initial fault was not reproduced by the
diagnostic candidate. These are small samples with no physically
verified power-off cycle. They do not establish a difference in
failure rates or a causal fix; keep DEBUG_LIST as an experimental
diagnostic, not a production change. SMP remains enabled throughout.
The baseline fault predates FPU enablement, so FPU work remains
independent of the boot-instability investigation.

The original release kernel was restored, and all five original
flash-component digests matched after rollback.
The first post-restore console attempt timed out. A single read-only
ROM/RTS-assisted recovery restored the console. The final baseline
profile confirmed riscv32, Linux 6.18.0+, harts 0–1 online, read-only
CramFS, and no advertised F/D (HWCAP 0x1105; F=0 D=0 HWPROBE_FD=0).
The baseline executable digest matched, its expected no-F skip passed,
and the temporary target artifact was removed. UART was released.
The timeout is recorded separately; its signature is unknown and is
not assumed to be the earlier VMA Oops. No permanent remediation
or production qualification is claimed.

## Peripheral removal control (2026-11-26)

The operator physically unplugged the USB mass-storage device and
suggested a power/thermal margin hypothesis. No firmware changes,
flash writes, or other intentional hardware changes were made for
this control. The original v1 image had already been restored and
all five original component digests verified earlier. The DWC2
host controller remained enabled; no external USB device enumeration
was found in these captures. Input voltage/current and temperature
were not measured.

Retrospective timestamps in three earlier failing captures with USB
storage present (seconds since the respective Linux boot):

| Capture | Mass storage detected | First Oops | Delta |
|---|---:|---:|---:|
| Matched baseline initial | 2.256347 | 2.281889 | 25.542 ms |
| Earlier matrix warm-05 | 2.278213 | 2.285487 | 7.274 ms |
| Earlier matrix warm-05-recovery | 2.264503 | 2.268210 | 3.707 ms |

The matched baseline initial capture also records SCSI host
registration at 2.272666 s, 9.223 ms before the first Oops.
This temporal association is not evidence of a causal mechanism:
normal init activity and USB discovery may overlap in time.

Six warm software resets (7.91–8.07 s) and six ROM/RTS-assisted
resets (6.91–6.96 s) passed, using the same no-F two-hart profile,
exact release command line, CramFS root, distinct boot IDs,
and 3 s settle as in the earlier matched suite. Initial boot also
passed. Across all 13 captures, both harts and DWC2 were present;
no external USB enumeration, Oops, panic, list corruption diagnostic,
or brownout message was found. The suite exited 0; UART released.
ROM/RTS resets do not establish complete physical power removal.

Private logs:
Orion, ~/source/build/s31-debug-list-matched-20261126/no-usb-20261126T223505Z/.
Earlier matrix captures under
~/source/build/s31-reboot-matrix-20261126/trials/.

Reference: official board schematic (J4/U5 USB host current limit
and U4 regulator sharing VCC_5V):
https://dl.espressif.com/schematics/esp32-s31-function-coreboard-1-schematics.pdf
The observation warrants separate power integrity and USB software
investigations. A powered USB hub with a non-backfeeding design can
help separate peripheral supply load from host activity; an
oscilloscope measurement of the 5 V input and 3.3 V rail during
enumeration/reset would provide direct electrical evidence.
Do not infer a power fault, DMA defect, or a fixed SMP race from
these small, nonrandomized samples. No source changes proposed.

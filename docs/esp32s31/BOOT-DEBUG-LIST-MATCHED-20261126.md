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

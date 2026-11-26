# S31 native Wi-Fi radio experiments — 2026-11-25

This is a **source-only engineering checkpoint**, separate from the validated vendor baseline and the October SPL-only community profile. It does not replace either baseline or authorize binary redistribution. See the [lock manifest](../../dependencies/s31-wifi-experiments.lock.json) for source pins, artifact sizes and hashes, and explicit test limits.

## Results

| Radio payload | XIP bytes | Hardware validation |
| --- | ---: | --- |
| Combined reference | 1,565,056 | boot, passive scan, reboot |
| Wi-Fi-only | 740,304 | boot, scan, reboot; mixed WPA2 association, DHCP and ICMP using the experimental kernel module |
| Wi-Fi-only without IDF EAP | 646,432 | boot, scan, reboot only |

The Wi-Fi-only reduction is 52.70%; the EAP experiment reduces the original payload by 58.70%, mostly by removing open-source code from the EAP path. The flash partition size has not changed. Bluetooth is intentionally unavailable in the reduced radio. The default combined radio is retained. The new combined build was reproducible across profile switches but differed from the earlier reference hash; it still needs physical regression.

## Mixed-cipher interoperability

The legacy profile is a default-off read-only module parameter, `legacy_tkip=1`. It enables CCMP and TKIP in mac80211 software, retaining HT negotiation for mixed-cipher APs. Native TX HT/aggregation and HE are disabled in the experimental mode. The initial legacy-only negotiation received association status 18. The updated variant completed WPA2-PSK with pairwise CCMP and group TKIP. Four associations and DHCP acquisitions, 35/35 gateway ICMP responses (30 with 1,000-byte payload), and two module profile switches with four passive scans passed. These results do not prove pairwise TKIP, periodic group-key renewal, HTTP, throughput, or default-mode CCMP association. TKIP is obsolete and should not be used for production. The Wi-Fi-only radio bytes and Linux kernel image remained unchanged during the module experiment.

## Source recovery and review

Apply `0002-radio-wifi-only.patch` on the producer base in the manifest using `git am`; the optional `0003-radio-no-idf-eap.patch` builds on it. The latter is a separate experiment, not needed for TKIP. Apply `0004-softmac-software-tkip.mbox` with `git am` on the pinned kernel base, not the vendor BSP. Compare source and artifact hashes against the manifest before flash. The producer option is `S31_WIFI_ONLY=1`, with `--wifi-only` on `tools/build/radio_image.py`; the EAP experiment additionally uses `S31_NO_IDF_EAP=1` at the radio component build. Use the exact pinned IDF and toolchains from the producer. Do not label an experimental payload as a standard firmware-input archive.

Restore the locally preserved pre-TKIP rootfs and Wi-Fi-only radio slot using the verified checkpoint, respecting the community flash layout, not the vendor map. The original kernel and DTB are also preserved. The local rootfs requires manual tmpfs initialization and radio start. No credentials, raw network logs, device-specific backups, proprietary payloads or binaries are included here. Review the radio licensing notice before any distribution. The kernel patch retains GPL-2.0-only licensing. Producer source patches retain their upstream licensing.

Upstream kernel review: https://github.com/GrieferPig/linux-esp32-s31/pull/3 (draft; style warnings and untested features are explicit merge gates).

## Follow-up hardware quality tests

- The 646,432-byte no-IDF-EAP payload completed mixed-cipher WPA2-PSK association, DHCP and 5/5 gateway ICMP responses. Enterprise authentication itself remains untested.
- The rebuilt combined radio (SHA-256 `92c888ee18fee84ddd065b62029e783a04e7e0c284518ee8b0ef2c3afb2de87a`) completed mixed-cipher WPA2, DHCP and 5/5 gateway ICMP responses, followed by 10/10 1,000-byte-payload pings after a Bluetooth HCI probe. The HCI probe yielded no valid command-complete event; Bluetooth functionality and coexistence are not yet validated.
- Forcing pairwise TKIP produced association status 18 with HT disabled, as expected from mac80211 legacy restrictions. No workaround bypassing those restrictions was applied.
- The Wi-Fi-only radio slot was restored and flash verification succeeded after both experiments. The final post-restore boot was not independently checked. HTTP/throughput and periodic group rekey remain unvalidated.

Raw private network logs remain local; the manifest records their hashes and sanitized outcomes only.

## Bluetooth hardware follow-up

Correction: the earlier combined-payload WPA2 test ran in Wi-Fi-only mode because the temporary init script overrode the requested combo setting. It was a valid Wi-Fi regression, not a coexistence test. After correcting the local test image, the runtime reported combo mode with both frontends ready. Direct HCI Reset, Read Local Version and BLE advertising parameter/data/enable commands returned status zero. The Orion Intel AX201/BlueZ detected the test advertisement at -44 dBm; a Wi-Fi passive scan succeeded during advertising. The test script checked the stop marker too early, so advertising shutdown was not independently confirmed. Both original Wi-Fi-only radio slot and rootfs were restored with verified flash hashes. No claim of BLE connection/GATT, Classic pairing, A2DP, concurrent IP throughput, or default-mode WPA2 association. No identifiers or raw private logs are published.

## BLE GATT and IP coexistence follow-up

With verified combo mode, the board completed mixed-cipher WPA2 and DHCP, then accepted a BLE connection from the Orion Intel AX201. The minimal engineering ATT/GATT endpoint returned the expected initial value, accepted an eight-byte synthetic write, and returned the identical updated value. Gateway ICMP during the GATT test was 20/20 with 1,000-byte payloads, zero loss and RTT 9.063/9.897/11.806 ms (min/avg/max). The test does not establish sustained throughput, secure GATT pairing, full GATT conformance, or Classic/A2DP operation. Advertising shutdown was not independently verified. The Classic/A2DP harness did not execute due to a remote execution safety check. Wi-Fi-only radio and rootfs were restored with flash hash verification; post-restore boot remains unchecked.

## Final hardware connectivity review

This section supersedes the provisional limitations in the chronological results above. On the rebuilt combined payload, the default `legacy_tkip=0` driver negotiated WPA2-PSK with CCMP for both pairwise and group traffic, completed DHCP, and passed 15/15 gateway pings before the Bluetooth test and 5/5 afterward. The standard BlueZ flow discovered, paired and connected to the board over BR/EDR. PipeWire was linked directly to the test sink; the A2DP transport entered active state and the receiver logged SBC stereo configuration at 44.1 kHz, stream start and release, and AVRCP control. The BTstack demo auto-accepts SSP confirmation; this is an engineering interoperability test, not a production pairing-security validation. PCM integrity, decoder counters, physical audio output, throughput, periodic group rekey and explicit BLE advertising shutdown remain unverified. The no-EAP experiment is separate. No proprietary binary or private raw log is published.

Upstream RFCs are submitted for review, not production release or merge authorization:

- https://github.com/GrieferPig/esp32-s31-linux-firmware/pull/1 — optional Wi-Fi-only producer profile; packaging provenance remains a release gate.
- https://github.com/GrieferPig/linux-esp32-s31/pull/3 — default-off software TKIP interoperability; style/diagnostic trace cleanup and maintainer review remain merge gates.

## Review cleanup and final source integration

The final follow-up patches close the packaging-provenance and kernel-style review gates. Apply `0005-radio-profile-packaging-guard.patch` after `0002-radio-wifi-only.patch` for the Wi-Fi-only producer branch; `0003-radio-no-idf-eap.patch` is a separate historical experiment and has not been requalified with the new guard. Apply `0006-softmac-review-cleanup.patch` after the `0004-softmac-software-tkip.mbox` series. Both follow-up patches replay to the exact committed source trees.

Producer commit `93d624d14aa4d665c091c014ef5619eb994728ec` records the build profile and exact payload SHA-256, and rejects missing, stale or experimental records when creating standard combined-only schema-1 input archives. All 24 package tests pass. Profile switching combined → Wi-Fi-only → combined reproduced the combined relocatable byte for byte. Existing archive verification is unchanged; provenance is not publisher authentication or redistribution authorization.

Kernel commit `9594a0997641415ea8e643b96212d134676dbecb` removes the temporary association trace and fixes style: checkpatch reports zero errors, warnings and checks. The module rebuild succeeded; vmlinux, xipImage and the combined radio image remained identical. The updated rootfs and radio partitions passed flash hash verification. On the board, both default and experimental software-crypto modes booted the updated module, associated via WPA2 CCMP/CCMP, obtained DHCP and passed 15/15 ICMP each. The experimental mode also advertised TKIP. The AP was not changed back to mixed TKIP, and Bluetooth audio was not repeated after this cleanup. Earlier mixed-cipher and Bluetooth results remain attributable to the earlier tested revision. The local engineering rootfs defaults to legacy_tkip=0. All remaining throughput, rekey, physical audio and production qualification limitations remain explicit.

These source changes are integrated in the BSP; upstream merge remains at maintainers’ discretion.

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

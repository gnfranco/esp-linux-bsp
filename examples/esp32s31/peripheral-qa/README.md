# Isolated peripheral smoke tests

Cross-compile with the pinned RV32 Linux-musl toolchain, e.g.:

```sh
CC=riscv32-esp-linux-musl-gcc
$CC -static -Os -Wall -Wextra -Werror peripheral-qa.c -o peripheral-qa
$CC -static -Os -Wall -Wextra -Werror can-loopback-config.c -o can-loopback-config
```

Run on the board, from a writable tmpfs. Transfer binaries and check SHA-256.

- Codec: `./peripheral-qa codec /dev/i2c-0 0x18` reads the ES8311's ID and version 128 times with combined transactions; no codec registers are modified.
- Virtual CAN: create an isolated interface with `ip link add dev vcanqa type vcan`, bring it up, run `./peripheral-qa can vcanqa`, and delete vcanqa. This tests SocketCAN, not a hardware controller.
- USB mass storage: after confirming the intentionally attached test disk, `./usb-storage-read-test.sh /dev/sda` checks sysfs for a USB disk and reads the first 256 KiB twice into tmpfs, compares copies, and deletes them. Never mounts or writes to the disk. The second read may be cached; this is functional testing, not a USB throughput benchmark.
- CAN hardware loopback: apply the dedicated test-only DT overlay, leaving both GPIO35 and 36 unconnected externally. Never run the following commands on a network connected to a vehicle, machine or production bus. Use `./can-loopback-config can0 2000000` to enable isolated loopback, then `./peripheral-qa hardware-loopback can0` (classic, then CAN FD with BRS). For FD only, use `hardware-fd-no-brs` or `hardware-fd-brs`. Stop each interface with `ip link set can0 down` afterwards. Repeat for can1. Hardware modes explicitly disable the transmitting socket's local software echo and verify the received IDs, payload and BRS flag.

The test overlay is not for use with external CAN transceivers. Full differential-bus validation requires separate TX/RX pinmux routes, two transceivers, termination, and another node or two controllers on the same board.

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host-only tests of the DMA pool stats parser, no hardware access."""
import importlib.util
import pathlib
import sys
import unittest

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("qa", pathlib.Path(__file__).with_name("pool-stats-qa.py"))
qa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qa)

def encode(v):
    return "\n".join(f"{k} {n}" for k, n in v.items())

class PoolParser(unittest.TestCase):
    def setUp(self):
        self.v = {"page_bytes": 4096, "total_pages": 64, "used_pages": 20,
                  "free_pages": 44, "tracked_pages": 20, "peak_pages": 28,
                  "largest_allocatable_pages": 32, "allocation_failures": 0}
    def test_valid(self):
        self.assertEqual(qa.parse_stats(encode(self.v)), self.v)
    def test_full(self):
        v = self.v | {"used_pages": 64, "free_pages": 0, "tracked_pages": 64,
                      "peak_pages": 64, "largest_allocatable_pages": 0}
        self.assertEqual(qa.parse_stats(encode(v)), v)
    def test_invalid(self):
        for patch in ({"total_pages": 63}, {"free_pages": 45}, {"tracked_pages": 19},
                      {"peak_pages": 19}, {"peak_pages": 65},
                      {"largest_allocatable_pages": 48}, {"largest_allocatable_pages": 0},
                      {"page_bytes": 8192}):
            with self.subTest(patch=patch), self.assertRaises(RuntimeError):
                qa.parse_stats(encode(self.v | patch))
    def test_missing_duplicate(self):
        for bad in (encode(self.v) + "\nused_pages 20",
                    encode({k: n for k, n in self.v.items() if k != "peak_pages"})):
            with self.assertRaises(RuntimeError):
                qa.parse_stats(bad)
class ConsoleParser(unittest.TestCase):
    def test_counters(self):
        self.assertEqual(qa.parse_counters("echo \\nPOOL_IO %s %s %s\n\nPOOL_IO 66240 19 10\n"),
                         {"usb_sectors_read": 66240, "eth_rx": 19, "eth_tx": 10})
    def test_counter_reject(self):
        for out in ("POOL_IO 3 2", "POOL_IO 3 2 1\nPOOL_IO 4 2 1", "POOL_IO -1 2 3"):
            with self.subTest(out=out), self.assertRaises(RuntimeError):
                qa.parse_counters(out)
    def test_ping(self):
        self.assertEqual(qa.parse_ping("10 packets transmitted, 10 packets received, 0% packet loss"), (10, 10))
        self.assertEqual(qa.parse_ping("  10 packets transmitted, 9 received, 10% packet loss"), (10, 9))
    def test_ping_reject(self):
        for out in ("", "10 packets transmitted, 10 received\n10 packets transmitted, 9 received"):
            with self.subTest(out=out), self.assertRaises(RuntimeError):
                qa.parse_ping(out)
class CaptureTests(unittest.TestCase):
    class Serial:
        def __init__(self, finished):
            self.finished = finished
            self.data = b""
        def write(self, data):
            import re
            tag = re.search(rb"([0-9a-f]{16}) ", data)[1]
            self.data = b"partial\r\n"
            if self.finished:
                self.data += b"__RADIO" + tag + b"=0__\r\n"
        @property
        def in_waiting(self):
            return len(self.data)
        def read(self, n):
            value, self.data = self.data[:n], self.data[n:]
            return value
    def test_completed(self):
        self.assertEqual(qa.capture_command(self.Serial(True), "true", .1), ("partial\n", 0))
    def test_timeout_preserves_output(self):
        with self.assertRaises(qa.ConsoleCaptureError) as e:
            qa.capture_command(self.Serial(False), "true", .01)
        self.assertEqual(e.exception.partial, "partial\n")
if __name__ == "__main__":
    unittest.main()

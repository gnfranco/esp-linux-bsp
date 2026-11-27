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
if __name__ == "__main__":
    unittest.main()

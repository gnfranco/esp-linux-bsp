#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import hashlib
import importlib.util
import pathlib
import unittest
p=pathlib.Path(__file__).with_name("rcu-uart-control.py")
s=importlib.util.spec_from_file_location("control",p)
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class ControlTests(unittest.TestCase):
    def test_payload(self):
        self.assertEqual(len(m.payload()),81920)
        self.assertEqual(m.payload(),m.payload())
    def test_profile(self):
        text="riscv32\n0-1\nroot / cramfs ro 0 0\ntmpfs /tmp tmpfs rw 0 0\nETH0_PRESENT\nUSB_DISK_PRESENT\nLEASE_NODE\n"
        m.profile_check(text,"uncorrected")
        m.profile_check(text,"aligned")
        with self.assertRaises(RuntimeError):m.profile_check(text,"baseline")
        m.profile_check(text.replace("LEASE_NODE","NO_LEASE_NODE"),"baseline")
        with self.assertRaises(RuntimeError):m.profile_check(text.replace("LEASE_NODE",""),"baseline")
        for extra in ("dev /tmp ext4 rw 0 0", "dev /tmp/child ext4 rw 0 0","WRONG_LEASE_NODE"):
            with self.assertRaises(RuntimeError):m.profile_check(text+extra+"\n","aligned")
        for field in ("0-1","USB_DISK_PRESENT","ETH0_PRESENT","/ cramfs ro","/tmp tmpfs"):
            with self.assertRaises(RuntimeError):m.profile_check(text.replace(field,"bad"),"aligned")
    def test_usb_identity(self):
        text="USB_ID 1234 abcd 121077761 /sys/devices/usb1/1-1/1-1:1.0/host0/target0:0:0/0:0:0:0\nKingston \nDataTraveler 3.0 \n"
        self.assertEqual(m.usb_identity(text,"1234:abcd")[0:3],("1234","abcd","121077761"))
        for bad in (text.replace("121077761","42"),text.replace("Kingston","Other"),text.replace("/1-1/","/1-2/"),text.replace("1234 abcd","1234 9999"),text.replace("/1-1:1.0/","/1-1.2/1-1.2:1.0/")):
            with self.assertRaises(RuntimeError):m.usb_identity(bad,"1234:abcd")
    def test_boot(self):
        self.assertEqual(m.boot_id("x\n00000000-0000-0000-0000-000000000001\n"),"00000000-0000-0000-0000-000000000001")
        with self.assertRaises(RuntimeError):m.boot_id("none")
if __name__=="__main__":unittest.main()

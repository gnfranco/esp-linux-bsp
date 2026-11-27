#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib.util
import pathlib
import unittest

p = pathlib.Path(__file__).with_name("screen-kernel-log.py")
spec = importlib.util.spec_from_file_location("screen", p)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class LogScreen(unittest.TestCase):
    def test_self_detected(self):
        self.assertEqual(m.screen("[   23.407735] rcu: INFO: rcu_sched self-detected stall on CPU\r\n")[0][0], "rcu_stall")
    def test_historical(self):
        self.assertEqual(m.screen("[   23.417990] rcu: INFO: rcu_sched detected stalls on CPUs/tasks:\n")[0][0], "rcu_stall")
    def test_starvation(self):
        self.assertEqual(m.screen("[  90.01] rcu: rcu_preempt kthread starved for 6000 jiffies!\n")[0][0], "rcu_stall")
    def test_kernel_faults(self):
        for line in ["Oops [#1]", "BUG: bad page state", "Unable to handle kernel paging request", "Kernel panic", "WARNING: CPU: 0", "list_add corruption"]:
            self.assertEqual(m.screen("[ 2.2] "+line)[0][0], "kernel_fault")
    def test_usb_timeout(self):
        self.assertEqual(m.screen("\x1b[31m[ 7.377671] usb 1-1: device descriptor read/64, error -110\x1b[m")[0][0], "usb_timeout")
    def test_hung_task(self):
        self.assertEqual(m.screen("[  80.0] INFO: task worker:5 blocked for more than 60 seconds.")[0][0], "hung_task")
    def test_boot_info_no_match(self):
        self.assertEqual(m.screen("[ 0.000] rcu: Hierarchical RCU implementation.\n[ 0.000] rcu: RCU event tracing is enabled.\n[ 3.9] sd 0:0:0:0: Attached SCSI removable disk\n"), [])

if __name__ == "__main__":
    unittest.main()

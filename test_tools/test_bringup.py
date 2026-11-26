"""Host tool checks: never touch a physical serial device during these tests."""

import configparser
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("bringup", ROOT / "tools/bringup.py")
bringup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bringup)


class BringupTests(unittest.TestCase):
    def setUp(self):
        self.output = contextlib.redirect_stdout(io.StringIO())
        self.output.__enter__()
        self.addCleanup(self.output.__exit__, None, None, None)

    def test_all_targets_have_firmware_and_instructions(self):
        config = configparser.ConfigParser()
        config.read(ROOT / "platformio.ini")
        for part, (env, _) in bringup.PARTS.items():
            self.assertIn("env:" + env, config)
            source_filter = config["env:" + env]["build_src_filter"]
            self.assertTrue(source_filter)
            self.assertEqual(len(bringup.GUIDES[part]), 3)
            self.assertIn(part, bringup.LABELS)

    def test_bluetooth_is_not_offered_as_esp32(self):
        devices = [
            {"port": "/dev/cu.Bluetooth-Incoming-Port", "hwid": "n/a"},
            {"port": "/dev/cu.debug-console", "hwid": "n/a"},
            {"port": "/dev/cu.usbserial-123", "hwid": "USB VID:PID=10C4:EA60"},
            {"port": "/dev/ttyACM0", "hwid": "n/a"},
            {"port": "COM3", "hwid": "USB"},
        ]
        with patch.object(bringup.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, json.dumps(devices))):
            ports = bringup.usb_ports("pio")
        self.assertEqual([p["port"] for p in ports], ["/dev/cu.usbserial-123", "/dev/ttyACM0", "COM3"])

    def test_no_device_never_uploads(self):
        with patch("builtins.input", side_effect=["1", "0"]), \
             patch.object(bringup, "usb_ports", return_value=[]), \
             patch.object(bringup, "flash") as flash:
            self.assertEqual(bringup.menu("pio"), 0)
            flash.assert_not_called()

    def test_selected_port_is_used_not_first_discovered(self):
        devices = [{"port": "COM3"}, {"port": "COM4"}]
        with patch("builtins.input", side_effect=["1", "2", "0"]), \
             patch.object(bringup, "usb_ports", return_value=devices), \
             patch.object(bringup, "flash") as flash:
            bringup.menu("pio")
            flash.assert_called_once_with("pio", "oled", "COM4")

    def test_cancel_port_selection_never_uploads(self):
        with patch("builtins.input", side_effect=["1", "0", "0"]), \
             patch.object(bringup, "usb_ports", return_value=[{"port": "COM3"}]), \
             patch.object(bringup, "flash") as flash:
            bringup.menu("pio")
            flash.assert_not_called()

    def test_failed_upload_does_not_start_monitor(self):
        with patch.object(bringup.subprocess, "run", side_effect=subprocess.CalledProcessError(1, ["pio"])) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                bringup.flash("pio", "lock", "COM3")
            self.assertEqual(run.call_count, 1)
            self.assertIn("upload", run.call_args.args[0])

    def test_dry_run_never_executes_upload_or_monitor(self):
        with patch.object(bringup.subprocess, "run") as run:
            for part in bringup.PARTS:
                bringup.flash("pio", part, "TEST_ONLY", dry_run=True)
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()

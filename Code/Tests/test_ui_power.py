"""Check power line decoding without opening a Tk window."""
import importlib.util
import sys
from pathlib import Path
import unittest

sys.dont_write_bytecode = True

path = Path(__file__).resolve().parents[2] / "UI_Mini_IPA/Code/something.py"
spec = importlib.util.spec_from_file_location("serial_monitor", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Value:
    def __init__(self):
        self.value = ""

    def set(self, value):
        self.value = value


class PowerParsingTest(unittest.TestCase):
    def setUp(self):
        self.monitor = module.SerialMonitor.__new__(module.SerialMonitor)
        self.monitor._distance_line_buffer = ""
        self.monitor.laser_power_consumption_var = Value()
        self.monitor.system_power_consumption_var = Value()

    def test_split_lines_and_unscaled_large_values(self):
        self.monitor._process_distance_data("PD: 123")
        self.assertEqual(self.monitor.laser_power_consumption_var.value, "")
        self.monitor._process_distance_data("45\r\nPS: 65535\r\n")
        self.assertEqual(self.monitor.laser_power_consumption_var.value, "12345")
        self.assertEqual(self.monitor.system_power_consumption_var.value, "65535")

    def test_multiple_reports_and_alias(self):
        self.monitor._process_distance_data("PD: 1000\r\nPS: 2000\r\nPD: 0\r\n")
        self.assertEqual(self.monitor.laser_power_consumption_var.value, "0")
        self.assertEqual(self.monitor.system_power_consumption_var.value, "2000")
        self.monitor._process_distance_data("PL: 17\r\n")
        self.assertEqual(self.monitor.laser_power_consumption_var.value, "17")


if __name__ == "__main__":
    unittest.main()

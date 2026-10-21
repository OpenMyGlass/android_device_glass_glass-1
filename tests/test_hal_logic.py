# SPDX-License-Identifier: Apache-2.0
"""Build the host C tests of the HAL logic (tests/*_test.c) with the host C compiler and run them."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ["power/power_logic.c", "lights/lights_logic.c", "tests/hal_logic_test.c"]
SENSORS_SOURCES = ["sensors/sensors_logic.c", "tests/sensors_logic_test.c"]
CC = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")


@unittest.skipUnless(CC, "no host C compiler")
class HalLogicTest(unittest.TestCase):
    def build_and_run(self, name, sources, libs=()):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / name
            scratch = Path(tmp) / "root"
            scratch.mkdir()
            build = subprocess.run(
                [CC, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-DGLASS_HOST_TEST",
                 "-pthread", "-o", str(exe)] + [str(ROOT / s) for s in sources] + list(libs),
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe), str(scratch)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_hal_logic(self):
        self.build_and_run("hal_logic_test", SOURCES)

    def test_sensors_logic(self):
        self.build_and_run("sensors_logic_test", SENSORS_SOURCES, ["-lm"])


if __name__ == "__main__":
    unittest.main()

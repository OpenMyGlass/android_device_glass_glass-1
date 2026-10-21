# SPDX-License-Identifier: Apache-2.0
"""Build tests/hal_logic_test.c with the host C compiler and run it."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ["power/power_logic.c", "lights/lights_logic.c", "tests/hal_logic_test.c"]
CC = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")


@unittest.skipUnless(CC, "no host C compiler")
class HalLogicTest(unittest.TestCase):
    def test_hal_logic(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "hal_logic_test"
            scratch = Path(tmp) / "root"
            scratch.mkdir()
            build = subprocess.run(
                [CC, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-DGLASS_HOST_TEST",
                 "-pthread", "-o", str(exe)] + [str(ROOT / s) for s in SOURCES],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe), str(scratch)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()

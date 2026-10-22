# SPDX-License-Identifier: Apache-2.0
"""Kernel and SGX module come from glass-kernel; the tree builds no kernel inline."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KO = "pvrsrvkm_sgx540_120.ko"


def code(rel):
    """Non-comment lines of a makefile, stripped."""
    return [l.strip() for l in (ROOT / rel).read_text().splitlines()
            if l.strip() and not l.strip().startswith("#")]


class KernelTreeTest(unittest.TestCase):
    def test_no_inline_kernel_or_sgx_build(self):
        text = "\n".join(code("BoardConfig.mk"))
        for gone in ("TARGET_KERNEL_SOURCE", "TARGET_KERNEL_CONFIG", "SGX_MODULES",
                     "TARGET_KERNEL_MODULES", "GLASS_PVR"):
            self.assertNotIn(gone, text)
        self.assertFalse((ROOT / "tools" / "fetch-pvr.sh").exists())

    def test_kernel_and_module_come_from_glass_kernel_dir(self):
        lines = code("device.mk")
        self.assertIn("GLASS_KERNEL_DIR ?= vendor/glass/kernel", lines)
        self.assertIn("$(GLASS_KERNEL_DIR)/zImage:kernel \\", lines)
        self.assertIn(f"$(GLASS_KERNEL_DIR)/{KO}:system/lib/modules/{KO}", lines)

    def test_stock_module_and_bltsville_are_not_extracted(self):
        text = (ROOT / "proprietary-files.txt").read_text()
        self.assertNotIn(KO, text)
        self.assertNotIn("bltsville", text)

    def test_bltsville_comes_from_hardware_ti_omap4(self):
        self.assertIn("TI_OMAP_USE_BLTSVILLE := true", code("BoardConfig.mk"))


@unittest.skipUnless(shutil.which("make"), "needs make")
class KernelSelectionTest(unittest.TestCase):
    """Evaluate device.mk's kernel block and BoardConfig.mk's switch with make."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        text = (ROOT / "device.mk").read_text()
        start = text.index("GLASS_KERNEL_DIR ?=")
        end = text.index("endif", start) + len("endif")
        (self.tmp / "kernel.mk").write_text(
            text[start:end] + "\n$(info COPY=$(strip $(PRODUCT_COPY_FILES)))\nall:\n\t@true\n")

    def make(self, *args, mk="kernel.mk"):
        return subprocess.run(["make", "-s", "-f", mk, *args], cwd=self.tmp,
                              capture_output=True, text=True)

    def test_copies_kernel_and_module_when_present(self):
        (self.tmp / "k").mkdir()
        (self.tmp / "k" / "zImage").write_text("")
        (self.tmp / "k" / KO).write_text("")
        r = self.make("GLASS_KERNEL_DIR=k")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn(f"COPY=k/zImage:kernel k/{KO}:system/lib/modules/{KO}", r.stdout)

    def test_stops_without_them(self):
        (self.tmp / "k").mkdir()
        (self.tmp / "k" / "zImage").write_text("")
        r = self.make("GLASS_KERNEL_DIR=k")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("tools/fetch-kernel.sh", r.stderr)

    def test_system_only_build(self):
        r = self.make("GLASS_KERNEL_DIR=missing", "GLASS_NO_KERNEL=true")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("COPY=\n", r.stdout + "\n")
        text = (ROOT / "BoardConfig.mk").read_text()
        start = text.index("ifeq ($(GLASS_NO_KERNEL),true)")
        end = text.index("endif", start) + len("endif")
        (self.tmp / "board.mk").write_text(
            text[start:end] + "\n$(info NO_KERNEL=$(TARGET_NO_KERNEL))\nall:\n\t@true\n")
        self.assertIn("NO_KERNEL=true", self.make("GLASS_NO_KERNEL=true", mk="board.mk").stdout)
        self.assertIn("NO_KERNEL=\n", self.make(mk="board.mk").stdout)


if __name__ == "__main__":
    unittest.main()

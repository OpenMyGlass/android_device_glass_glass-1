# SPDX-License-Identifier: Apache-2.0
"""The SGX module is built from DDK 1.9@2166536, as glass-kernel builds it."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KO = "pvrsrvkm_sgx540_120.ko"
# glass-kernel scripts/source.env and scripts/build.sh.
PINS = {
    "PVR_REPO": "https://github.com/KFire-Android/android_vendor_amazon_otter.git",
    "PVR_COMMIT": "7d7692662a6d906fe074475602ae3085aa4e35cd",
    "PVR_PATH": "proprietary/imgtec/eurasia_km",
    "PVR_EXPECTED_BUILD": "2166536",
    "PVR_EXPECTED_BRANCH": "1.9",
}
DDK_ARGS = ("KERNELDIR=", "BUILD=release", "TARGET_PRODUCT=blaze_tablet", "TARGET_SGX=540",
            "PLATFORM_VERSION=4.0", "KERNEL_CROSS_COMPILE=", "CROSS_COMPILE=", "kbuild")


def pins():
    out = {}
    for line in (ROOT / "tools" / "pvr.env").read_text().splitlines():
        if line and not line.startswith("#"):
            k, v = line.split("=", 1)
            out[k] = v
    return out


class PinsTest(unittest.TestCase):
    def test_pins_match_glass_kernel(self):
        self.assertEqual(pins(), PINS)

    def test_boardconfig_builds_from_the_pinned_path(self):
        text = (ROOT / "BoardConfig.mk").read_text()
        self.assertIn("GLASS_PVR_KM ?= $(GLASS_PVR_FETCH_DIR)/" + PINS["PVR_PATH"] + "\n", text)
        self.assertNotIn("pvr-source", text)

    def test_stock_module_is_not_extracted(self):
        self.assertNotIn(KO, (ROOT / "proprietary-files.txt").read_text())


@unittest.skipUnless(shutil.which("make"), "needs make")
class SgxRuleTest(unittest.TestCase):
    """Run the SGX_MODULES rule against a fake DDK that records how it was called."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        ddk = self.tmp / "ddk"
        (ddk / "include4").mkdir(parents=True)
        (ddk / "include4" / "pvrversion.h").write_text("#define PVRVERSION_BUILD 2166536\n")
        mk = ddk / "eurasiacon" / "build" / "linux2" / "omap4430_android"
        mk.mkdir(parents=True)
        (mk / "Makefile").write_text(
            "kbuild:\n\tmkdir -p $(OUT)/target\n"
            "\techo '$(MAKECMDGOALS) $(MAKEOVERRIDES)' > $(OUT)/target/" + KO + "\n")
        tool = self.tmp / "tool"
        tool.mkdir()
        strip = tool / "arm-eabi-strip"
        strip.write_text("#!/bin/sh\necho \"$@\" > \"$2.stripped\"\n")
        strip.chmod(0o755)
        (self.tmp / "k" / "obj").mkdir(parents=True)
        (self.tmp / "modules").mkdir()
        self.ddk, self.tool = ddk, tool

    def make(self, ddk):
        return subprocess.run(
            ["make", "-s", "-f", str(ROOT / "BoardConfig.mk"), "SGX_MODULES",
             f"GLASS_PVR_KM={ddk}", f"KERNEL_OUT={self.tmp}/k/obj",
             f"KERNEL_MODULES_OUT={self.tmp}/modules", f"ARM_EABI_TOOLCHAIN={self.tool}"],
            cwd=self.tmp, capture_output=True, text=True)

    def test_builds_a_copy_of_the_ddk_with_glass_kernels_options(self):
        r = self.make(self.ddk)
        self.assertEqual(r.returncode, 0, r.stderr)
        args = (self.tmp / "modules" / KO).read_text().split()
        for want in DDK_ARGS:
            self.assertTrue(any(a.startswith(want) for a in args), (want, args))
        self.assertIn(f"OUT={self.tmp}/k/obj/../pvr-build/binary", args)
        self.assertTrue((self.tmp / "modules" / (KO + ".stripped")).exists())
        self.assertFalse((self.ddk / "binary").exists(), "the DDK checkout must stay pristine")

    def test_fails_without_the_ddk(self):
        r = self.make(self.tmp / "missing")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("tools/fetch-pvr.sh", r.stderr)


if __name__ == "__main__":
    unittest.main()

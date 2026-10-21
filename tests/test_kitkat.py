# SPDX-License-Identifier: Apache-2.0
"""The 4.4.4 branch: kernel and SGX module from glass-kernel, no inline build (docs/kitkat.md)."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KO = "pvrsrvkm_sgx540_120.ko"
# KEYWORD() names in system/core/init/keywords.h at android-4.4.4_r2.0.1.
INIT_KEYWORDS_4_4 = set("""
    capability chdir chroot class class_start class_stop class_reset console critical disabled
    domainname exec export group hostname ifup insmod import keycodes mkdir mount_all mount on
    oneshot onrestart powerctl restart restorecon rm rmdir seclabel service setcon setenforce
    setenv setkey setprop setrlimit setsebool socket start stop swapon_all trigger symlink
    sysclktz user wait write copy chown chmod loglevel load_persist_props ioprio
""".split())
# fs_mgr flags in system/core/fs_mgr/fs_mgr.c at android-4.4.4_r2.0.1 (with "=" for valued ones).
FS_MGR_FLAGS_4_4 = set("""
    defaults wait check encryptable= nonremovable voldmanaged= length= recoveryonly swapprio=
    zramsize= verify noemulatedsd
""".split())


def code(rel):
    """Non-comment lines of a makefile, stripped."""
    return [l.strip() for l in (ROOT / rel).read_text().splitlines()
            if l.strip() and not l.strip().startswith("#")]


class KitKatTreeTest(unittest.TestCase):
    def test_no_inline_kernel_or_sgx_build(self):
        text = "\n".join(code("BoardConfig.mk"))
        self.assertIn("TARGET_KERNEL_SOURCE := device/glass/glass-1/no-inline-kernel", code("BoardConfig.mk"))
        self.assertFalse((ROOT / "no-inline-kernel").exists())
        text = text.replace("TARGET_KERNEL_SOURCE := device/glass/glass-1/no-inline-kernel", "")
        for gone in ("TARGET_KERNEL_SOURCE", "TARGET_KERNEL_CONFIG", "SGX_MODULES",
                     "TARGET_KERNEL_MODULES", "GLASS_PVR", "WITH_DEXPREOPT_PIC"):
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

    def test_init_files_use_only_4_4_keywords(self):
        for rc in sorted((ROOT / "rootdir").glob("*.rc")):
            if rc.name.startswith("ueventd."):
                continue
            for n, line in enumerate(rc.read_text().splitlines(), 1):
                words = line.split()
                if words and not words[0].startswith("#"):
                    with self.subTest(file=rc.name, line=n):
                        self.assertIn(words[0], INIT_KEYWORDS_4_4)

    def test_fstab_uses_only_4_4_flags(self):
        for line in (ROOT / "rootdir" / "fstab.omap4430").read_text().splitlines():
            fields = line.split()
            if len(fields) == 5 and not fields[0].startswith("#"):
                for flag in fields[4].split(","):
                    key = flag.split("=", 1)[0] + ("=" if "=" in flag else "")
                    with self.subTest(flag=flag):
                        self.assertIn(key, FS_MGR_FLAGS_4_4)


@unittest.skipUnless(shutil.which("make"), "needs make")
class KernelSelectionTest(unittest.TestCase):
    """Evaluate device.mk's kernel block with make, as product configuration does."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        text = (ROOT / "device.mk").read_text()
        start = text.index("GLASS_KERNEL_DIR ?=")
        end = text.index("endif", start) + len("endif")
        (self.tmp / "kernel.mk").write_text(
            text[start:end] + "\n$(info COPY=$(strip $(PRODUCT_COPY_FILES)))\nall:\n\t@true\n")

    def make(self, *args):
        return subprocess.run(["make", "-s", "-f", "kernel.mk", *args], cwd=self.tmp,
                              capture_output=True, text=True)

    def test_copies_kernel_and_module_when_present(self):
        (self.tmp / "k").mkdir()
        (self.tmp / "k" / "zImage").write_text("")
        (self.tmp / "k" / KO).write_text("")
        r = self.make("GLASS_KERNEL_DIR=k")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn(f"COPY=k/zImage:kernel k/{KO}:system/lib/modules/{KO}", r.stdout)

    def test_stops_with_a_kernel_but_no_module(self):
        (self.tmp / "k").mkdir()
        (self.tmp / "k" / "zImage").write_text("")
        r = self.make("GLASS_KERNEL_DIR=k")
        self.assertNotEqual(r.returncode, 0)

    def test_stops_without_a_kernel(self):
        r = self.make("GLASS_KERNEL_DIR=missing")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("tools/fetch-kernel.sh", r.stderr)

    def test_system_only_build_without_a_kernel(self):
        r = self.make("GLASS_KERNEL_DIR=missing", "GLASS_NO_KERNEL=true")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("COPY=\n", r.stdout + "\n")

    def test_boardconfig_skips_boot_image_without_a_kernel(self):
        text = (ROOT / "BoardConfig.mk").read_text()
        self.assertIn("ifeq ($(GLASS_NO_KERNEL),true)\nTARGET_NO_KERNEL := true\nendif", text)


if __name__ == "__main__":
    unittest.main()

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

    def test_art_only_product(self):
        lines = code("full_glass_1.mk")
        inherit = lines.index("$(call inherit-product, $(SRC_TARGET_DIR)/product/aosp_base.mk)")
        runtimes = lines.index("PRODUCT_RUNTIMES := runtime_libart_default")
        self.assertGreater(runtimes, inherit, "must come after the AOSP product is inherited")
        self.assertEqual([l for l in lines if "PRODUCT_RUNTIMES" in l],
                         ["PRODUCT_RUNTIMES := runtime_libart_default"])
        for rel in ("device.mk", "aosp_glass_1.mk"):
            self.assertFalse([l for l in code(rel) if "PRODUCT_RUNTIMES" in l], rel)

    def test_no_dex_preopt(self):
        lines = code("BoardConfig.mk")
        self.assertIn("DISABLE_DEXPREOPT := true", lines)
        self.assertFalse([l for l in lines if l.startswith("WITH_DEXPREOPT")])

    def test_dex2oat_filter_defaults(self):
        props = dict(l.split("=", 1) for l in (ROOT / "system.prop").read_text().splitlines()
                     if l and not l.startswith("#"))
        self.assertEqual(props.get("persist.glass.d2o.sys"), "speed")
        self.assertEqual(props.get("persist.glass.d2o.usr"), "space")
        for key in props:
            self.assertLessEqual(len(key), 31, f"{key}: longer than 4.4's PROP_NAME_MAX - 1")
        self.assertNotIn("dalvik.vm.jit.codecachesize", props)

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

    def test_lean_overlay_and_heap(self):
        text = (ROOT / "overlay/frameworks/base/core/res/res/values/config.xml").read_text()
        for name in ("config_voice_capable", "config_sms_capable", "config_dreamsSupported"):
            self.assertIn(f'<bool name="{name}">false</bool>', text)
        self.assertNotIn('<item>"mobile', text)
        lines = code("device.mk")
        self.assertIn("$(call inherit-product, frameworks/native/build/phone-hdpi-512-dalvik-heap.mk)",
                      lines)
        self.assertFalse([l for l in lines if "xhdpi-1024" in l])


@unittest.skipUnless(shutil.which("make"), "needs make")
class RemovedPackagesTest(unittest.TestCase):
    """BoardConfig.mk filters the product's packages, as build/core/config.mk would read it."""

    def test_filter(self):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp)
        (tmp / "t.mk").write_text(
            "INTERNAL_PRODUCT := device/glass/glass-1/aosp_glass_1.mk\n"
            "PRODUCTS.$(INTERNAL_PRODUCT).PRODUCT_PACKAGES := Settings TeleService Home "
            "Launcher2 SystemUI LatinIME libwnndict\n"
            f"include {ROOT}/BoardConfig.mk\n"
            "all:\n\t@echo '$(strip $(PRODUCTS.$(INTERNAL_PRODUCT).PRODUCT_PACKAGES))'\n")
        r = subprocess.run(["make", "-s", "-f", "t.mk"], cwd=tmp, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout.split(), ["Settings", "TeleService", "Home", "SystemUI"])

    def test_keeps_what_the_system_needs(self):
        text = (ROOT / "BoardConfig.mk").read_text()
        block = text[text.index("GLASS_REMOVED_PACKAGES :="):text.index("PRODUCTS.$(INTERNAL")]
        removed = set(block.replace("\\", " ").split()[2:])
        for keep in ("Home", "Settings", "SystemUI", "Keyguard", "Bluetooth", "PackageInstaller",
                     "DocumentsUI", "DownloadProviderUi", "Provision", "ContactsProvider",
                     "TeleService", "TelephonyProvider", "PrintSpooler"):
            self.assertNotIn(keep, removed)
        self.assertIn("Browser", removed)


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

# SPDX-License-Identifier: Apache-2.0
"""Build tests/audio_logic_test.c with the host C compiler and run it; check the audio files."""
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_facts import facts_table, rc_commands

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ["audio/audio_logic.c", "tests/mock_tinyalsa.c", "tests/audio_logic_test.c"]
CC = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")


@unittest.skipUnless(CC, "no host C compiler")
class AudioLogicTest(unittest.TestCase):
    def test_audio_logic(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "audio_logic_test"
            scratch = Path(tmp) / "root"
            scratch.mkdir()
            build = subprocess.run(
                [CC, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-DGLASS_HOST_TEST",
                 "-o", str(exe)] + [str(ROOT / s) for s in SOURCES],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe), str(scratch)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


class AudioFilesTest(unittest.TestCase):
    def header_paths(self):
        text = (ROOT / "audio" / "audio_logic.h").read_text().replace("\\\n", " ")
        macros = dict(re.findall(r"#define (GLASS_\w+_PATH|GLASS_USB_MUX)\s+(.*?)(?:\s*/\*.*)?$",
                                 text, re.M))
        out = {}
        for name, value in macros.items():
            value = value.replace("GLASS_USB_MUX", macros["GLASS_USB_MUX"])
            out[name] = "".join(re.findall(r'"([^"]*)"', value))
        return out

    def test_media_can_write_its_sysfs_files(self):
        # The HAL runs in mediaserver (uid media). These two are chowned to media in
        # init.omap4430.rc per docs/hardware-facts.md; id_monitor is 0666 in the kernel.
        rows = {row["Path"]: row for row in facts_table("Sysfs")}
        paths = self.header_paths()
        for name in ("GLASS_MUX_MODE_PATH", "GLASS_PAUSE_FOR_AUDIO_PATH"):
            with self.subTest(path=paths[name]):
                row = rows.get(paths[name])
                self.assertIsNotNone(row, "not in the Sysfs facts")
                self.assertEqual((row["User"], row["Mode"]), ("media", "0664"))
                cmds = rc_commands("rootdir/init.omap4430.rc")
                self.assertIn(["chown", "media", "media", paths[name]], cmds)
                self.assertIn(["chmod", "0664", paths[name]], cmds)

    def test_policy(self):
        conf = (ROOT / "audio" / "audio_policy.conf").read_text()
        self.assertIn("default_output_device AUDIO_DEVICE_OUT_SPEAKER", conf)
        primary = conf[conf.index("  primary {"):conf.index("  a2dp {")]
        out = primary[primary.index("outputs {"):primary.index("inputs {")]
        self.assertRegex(out, r"sampling_rates 48000\n")
        self.assertIn("AUDIO_OUTPUT_FLAG_PRIMARY", out)
        self.assertEqual(conf.count("{"), conf.count("}"))
        mk = (ROOT / "device.mk").read_text()
        self.assertIn("audio/audio_policy.conf:system/etc/audio_policy.conf", mk)
        blobs = (ROOT / "proprietary-files.txt").read_text().split()
        self.assertNotIn("etc/audio_policy.conf", blobs)
        self.assertNotIn("lib/hw/audio.primary.glass_1.so", blobs)


if __name__ == "__main__":
    unittest.main()

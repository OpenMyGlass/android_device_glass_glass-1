# SPDX-License-Identifier: Apache-2.0
"""Unit tests for the Android.mk reading and symbol checks of tools/build-hals.py."""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("build_hals", ROOT / "tools" / "build-hals.py")
build_hals = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(build_hals)

HALS = ("power", "lights")


class AndroidMkTest(unittest.TestCase):
    def test_hal_makefiles(self):
        for hal in HALS:
            with self.subTest(hal=hal):
                mk = build_hals.parse_android_mk((ROOT / hal / "Android.mk").read_text())
                self.assertEqual(mk["LOCAL_MODULE"], [f"{hal}.glass_1"])
                for src in mk["LOCAL_SRC_FILES"]:
                    self.assertTrue((ROOT / hal / src).is_file(), src)
                self.assertIn("-Werror", mk["LOCAL_CFLAGS"])
                self.assertEqual(mk["LOCAL_SHARED_LIBRARIES"], ["liblog"])

    def test_continuation_and_append(self):
        mk = build_hals.parse_android_mk(
            "LOCAL_PATH := $(call my-dir)\ninclude $(CLEAR_VARS)\n"
            "LOCAL_MODULE := x.glass_1\nLOCAL_SRC_FILES := a.c \\\n    b.c # comment\n"
            "LOCAL_SRC_FILES += c.c\ninclude $(BUILD_SHARED_LIBRARY)\n")
        self.assertEqual(mk["LOCAL_SRC_FILES"], ["a.c", "b.c", "c.c"])
        self.assertEqual(mk["LOCAL_SHARED_LIBRARIES"], [])

    def test_rejects_functions_and_two_modules(self):
        with self.assertRaises(ValueError):
            build_hals.parse_android_mk("include $(CLEAR_VARS)\nLOCAL_MODULE := x\n"
                                        "LOCAL_SRC_FILES := $(wildcard *.c)\n")
        with self.assertRaises(ValueError):
            build_hals.parse_android_mk("include $(CLEAR_VARS)\nLOCAL_MODULE := x\n"
                                        "LOCAL_SRC_FILES := a.c\ninclude $(CLEAR_VARS)\n")


class SymbolsTest(unittest.TestCase):
    NDK = {"libc": {"open", "write"}, "liblog": {"__android_log_print"},
           "libm": set(), "libdl": set()}

    def test_libc_always_allowed(self):
        self.assertEqual(build_hals.undefined_problems({"open"}, self.NDK, {}, []), [])

    def test_liblog_only_when_listed(self):
        sym = {"__android_log_print"}
        self.assertEqual(build_hals.undefined_problems(sym, self.NDK, {}, ["liblog"]), [])
        self.assertEqual(build_hals.undefined_problems(sym, self.NDK, {}, []),
                         ["__android_log_print"])

    def test_allowlist(self):
        allow = build_hals.read_allowlist("# c\nlibcutils property_get\n")
        self.assertEqual(allow, {"libcutils": {"property_get"}})
        sym = {"property_get", "mystery"}
        self.assertEqual(build_hals.undefined_problems(sym, self.NDK, allow, ["libcutils"]),
                         ["mystery"])
        self.assertEqual(build_hals.undefined_problems(sym, self.NDK, allow, []),
                         ["mystery", "property_get"])
        with self.assertRaises(ValueError):
            build_hals.read_allowlist("libcutils\n")

    def test_shipped_allowlist_parses(self):
        build_hals.read_allowlist(build_hals.ALLOW_FILE.read_text())


if __name__ == "__main__":
    unittest.main()

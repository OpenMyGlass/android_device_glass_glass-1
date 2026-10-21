# SPDX-License-Identifier: Apache-2.0
"""The hardware composer is the source-built hwcomposer.omap4, never the stock blob."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def entries(rel):
    out = []
    for line in (ROOT / rel).read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.extend(p.strip() for p in line.split(":"))
    return out


class HwcTest(unittest.TestCase):
    def test_built_from_source(self):
        text = (ROOT / "device.mk").read_text()
        self.assertRegex(text, r"(?m)^\s*hwcomposer\.omap4\s*$")

    def test_no_stock_hwc_blobs(self):
        # The stock HWC and the writeback library it links; the source HWC needs neither.
        for path in entries("proprietary-files.txt"):
            with self.subTest(path=path):
                name = path.rsplit("/", 1)[-1]
                self.assertFalse(re.match(r"hwcomposer\.", name), path)
                self.assertNotEqual(name, "libdsswbhal.so", path)


if __name__ == "__main__":
    unittest.main()

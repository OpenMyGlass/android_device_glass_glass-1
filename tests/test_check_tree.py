# SPDX-License-Identifier: Apache-2.0
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

import check_tree  # noqa: E402

SPDX = "# SPDX-License-Identifier: Apache-2.0\n"


class CheckTreeTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.write("device.mk", "PRODUCT_COPY_FILES += \\\n"
                   "    $(LOCAL_PATH)/rootdir/fstab.omap4430:root/fstab.omap4430\n")
        self.write("rootdir/fstab.omap4430", SPDX + "# fstab\n")
        self.write("proprietary-files.txt", SPDX + "#\n# A\na\nb\n\n# B\na\n")

    def write(self, rel, data):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data if isinstance(data, bytes) else data.encode())

    def test_clean_tree(self):
        self.assertEqual(check_tree.check(self.root), [])

    def test_missing_copy_source(self):
        (self.root / "rootdir/fstab.omap4430").unlink()
        problems = check_tree.check(self.root)
        self.assertEqual(len(problems), 1)
        self.assertIn("rootdir/fstab.omap4430", problems[0])

    def test_device_path_form(self):
        self.write("x.mk", "    device/glass/glass-1/x.conf:system/etc/x.conf\n")
        problems = check_tree.check(self.root)
        self.assertEqual(len(problems), 1)
        self.assertIn("x.conf", problems[0])
        self.write("x.conf", "k=v\n")
        self.assertEqual(check_tree.check(self.root), [])

    def test_binary_file_rejected(self):
        self.write("blob.dat", b"ab\x00cd")
        problems = check_tree.check(self.root)
        self.assertEqual(len(problems), 1)
        self.assertIn("blob.dat", problems[0])

    def test_large_file_rejected(self):
        self.write("big.txt", "a" * 65537)
        problems = check_tree.check(self.root)
        self.assertEqual(len(problems), 1)
        self.assertIn("big.txt", problems[0])

    def test_list_duplicates_and_order(self):
        self.write("proprietary-files.txt", SPDX + "# A\na\na\n")
        problems = check_tree.check(self.root)
        self.assertEqual(len(problems), 1)
        self.assertIn("duplicate", problems[0])
        self.write("proprietary-files.txt", SPDX + "# A\nb\na\n")
        problems = check_tree.check(self.root)
        self.assertEqual(len(problems), 1)
        self.assertIn("order", problems[0])

    def test_new_file_needs_spdx(self):
        self.write("rootdir/init.rc", "on boot\n")
        problems = check_tree.check(self.root)
        self.assertEqual(len(problems), 1)
        self.assertIn("rootdir/init.rc", problems[0])
        self.assertIn("SPDX", problems[0])


if __name__ == "__main__":
    unittest.main()

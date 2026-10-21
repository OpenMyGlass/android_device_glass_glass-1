# SPDX-License-Identifier: Apache-2.0
"""Check rootdir/ and system.prop against the tables in docs/hardware-facts.md."""
import re
import shlex
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FACTS = ROOT / "docs" / "hardware-facts.md"
ROOTDIR = ROOT / "rootdir"
BY_NAME = "/dev/block/platform/omap/omap_hsmmc.1/by-name/"
USB_SYSFS = "/sys/class/android_usb/android0/"


def facts_table(heading):
    """Rows of the first table under '## <heading>', as dicts without Source."""
    lines = FACTS.read_text().splitlines()
    try:
        start = lines.index("## " + heading) + 1
    except ValueError:
        raise AssertionError(f"no section '## {heading}' in {FACTS}")
    table = []
    for line in lines[start:]:
        if line.startswith("## "):
            break
        if line.startswith("|"):
            table.append([c.strip() for c in line.strip().strip("|").split("|")])
        elif table:
            break
    if len(table) < 3:
        raise AssertionError(f"no table under '## {heading}'")
    header = table[0]
    keep = len(header) - 1 if header[-1] == "Source" else len(header)
    return [dict(zip(header[:keep], row[:keep])) for row in table[2:]]


def read(rel):
    path = ROOT / rel
    if not path.is_file():
        raise AssertionError(f"{rel} does not exist")
    return path.read_text()


def config_lines(rel):
    """Token lists of every non-blank, non-comment line."""
    out = []
    for line in read(rel).splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.append(shlex.split(line))
    return out


def rc_blocks(rel):
    """List of (header tokens, [command tokens]) for each on/service block."""
    blocks = []
    for tokens in config_lines(rel):
        if tokens[0] in ("on", "service", "import"):
            blocks.append((tokens, []))
        elif blocks:
            blocks[-1][1].append(tokens)
    return blocks


def rc_commands(rel):
    return [cmd for header, cmds in rc_blocks(rel) if header[0] == "on" for cmd in cmds]


class FactsTest(unittest.TestCase):
    def test_partitions(self):
        entries = {}
        for fields in config_lines("rootdir/fstab.omap4430"):
            self.assertEqual(len(fields), 5, f"fstab line {fields}")
            entries[fields[1]] = fields
        for row in facts_table("Partitions"):
            name = row["Name"]
            device = "/dev/block/zram0" if name == "zram0" else BY_NAME + name
            want = [device, row["Mount point"], row["Type"],
                    row["Mount options"], row["fs_mgr flags"]]
            with self.subTest(mount=row["Mount point"]):
                self.assertEqual(entries.get(row["Mount point"]), want)

    def test_nodes(self):
        nodes = {}
        for fields in config_lines("rootdir/ueventd.omap4430.rc"):
            nodes[fields[0]] = fields[1:]
        for row in facts_table("Device nodes"):
            with self.subTest(path=row["Path"]):
                self.assertEqual(nodes.get(row["Path"]),
                                 [row["Mode"], row["User"], row["Group"]])

    def test_sysfs(self):
        chowns, chmods = {}, {}
        for cmd in rc_commands("rootdir/init.omap4430.rc"):
            if cmd[0] == "chown":
                chowns.setdefault(cmd[3], []).append(cmd[1:3])
            elif cmd[0] == "chmod":
                chmods.setdefault(cmd[2], []).append(cmd[1])
        for row in facts_table("Sysfs"):
            path = row["Path"]
            with self.subTest(path=path):
                self.assertEqual(chowns.get(path), [[row["User"], row["Group"]]])
                if row["Mode"] == "-":
                    self.assertNotIn(path, chmods)
                else:
                    self.assertEqual(chmods.get(path), [row["Mode"]])

    def test_services(self):
        services = {}
        for header, opts in rc_blocks("rootdir/init.omap4430.rc"):
            if header[0] == "service":
                self.assertNotIn(header[1], services, "service defined twice")
                services[header[1]] = (header[2:], opts)
        args = {row["Name"]: row for row in facts_table("Service arguments")}
        for row in facts_table("Services"):
            name = row["Name"]
            with self.subTest(service=name):
                self.assertIn(name, services)
                command, opts = services[name]
                options = {o[0]: o[1:] for o in opts}
                self.assertEqual(command[0], row["Program"])
                self.assertEqual(options.get("class"), [row["Class"]])
                for key, column in (("user", "User"), ("group", "Group")):
                    want = None if row[column] == "-" else [row[column]]
                    self.assertEqual(options.get(key), want, key)
                self.assertEqual("oneshot" in options, row["Oneshot"] == "yes")
                extra = args[name]
                if extra["Arguments"] == "none":
                    self.assertEqual(command[1:], [])
                elif extra["Arguments"].startswith("-"):
                    self.assertEqual(command[1:], extra["Arguments"].split())
                self.assertEqual("disabled" in options,
                                 extra["Options"].startswith("disabled"))

    def test_properties(self):
        props = {}
        for line in read("system.prop").splitlines():
            if line.strip() and not line.startswith("#"):
                key, _, value = line.partition("=")
                self.assertNotIn(key, props, "property set twice")
                props[key] = value
        for row in facts_table("Properties"):
            with self.subTest(prop=row["Property"]):
                self.assertEqual(props.get(row["Property"]), row["Value"])

    def test_usb(self):
        triggers = {}
        for header, cmds in rc_blocks("rootdir/init.omap4430.usb.rc"):
            m = re.fullmatch(r"property:sys\.usb\.config=(\S+)", " ".join(header[1:]))
            if header[0] == "on" and m:
                triggers[m.group(1)] = cmds
        for row in facts_table("USB"):
            fset = row["Function set"]
            with self.subTest(functions=fset):
                self.assertIn(fset, triggers)
                cmds = triggers[fset]
                writes = {c[1][len(USB_SYSFS):]: c[2] for c in cmds
                          if c[0] == "write" and c[1].startswith(USB_SYSFS)}
                self.assertEqual(writes.get("idVendor"), row["idVendor"])
                self.assertEqual(writes.get("idProduct"), row["idProduct"])
                self.assertEqual(writes.get("functions"), fset)
                self.assertEqual(writes.get("enable"), "1")
                self.assertEqual(writes.get("bDeviceClass"),
                                 "224" if "rndis" in fset.split(",") else None)
                self.assertEqual(["start", "adbd"] in cmds,
                                 "adb" in fset.split(","))


if __name__ == "__main__":
    unittest.main()

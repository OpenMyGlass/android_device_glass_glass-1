# SPDX-License-Identifier: Apache-2.0
"""Check media_codecs.xml, media_profiles.xml and device.mk against docs/media-facts.md."""
import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FACTS = ROOT / "docs" / "media-facts.md"
GOOGLE_DATA = "frameworks/av/media/libstagefright/data/"


def table(heading):
    """Rows of the first table under '## <heading>', as dicts without Source."""
    lines = FACTS.read_text().splitlines()
    try:
        start = lines.index("## " + heading) + 1
    except ValueError:
        raise AssertionError(f"no section '## {heading}' in {FACTS}")
    rows = []
    for line in lines[start:]:
        if line.startswith("## "):
            break
        if line.startswith("|"):
            rows.append([c.strip() for c in line.strip().strip("|").split("|")])
        elif rows:
            break
    if len(rows) < 3:
        raise AssertionError(f"no table under '## {heading}'")
    header = rows[0]
    keep = len(header) - 1 if header[-1] == "Source" else len(header)
    return [dict(zip(header[:keep], row[:keep])) for row in rows[2:]]


def split(cell):
    return [c.strip() for c in cell.split(",") if c.strip()]


def attrs(cell):
    return dict(pair.split("=", 1) for pair in cell.split())


def xml_root(name):
    return ET.parse(ROOT / name).getroot()


class MediaCodecsTest(unittest.TestCase):
    def setUp(self):
        self.root = xml_root("media_codecs.xml")

    def test_hardware_codecs(self):
        got = []
        for section, kind in (("Decoders", "decoder"), ("Encoders", "encoder")):
            sections = self.root.findall(section)
            self.assertEqual(len(sections), 1, section)
            for codec in sections[0]:
                self.assertEqual(codec.tag, "MediaCodec")
                types = [t.get("name") for t in codec.findall("Type")]
                if codec.get("type"):
                    types.insert(0, codec.get("type"))
                got.append((kind, codec.get("name"), types,
                            [q.get("name") for q in codec.findall("Quirk")]))
        want = [(r["Kind"], r["Name"], split(r["Types"]), split(r["Quirks"]))
                for r in table("Hardware codecs")]
        self.assertEqual(got, want)

    def test_includes(self):
        children = [c.tag if c.tag != "Include" else c.get("href") for c in self.root]
        want = []
        rows = table("Software codec lists (6.0)")
        want += [r["Include"] for r in rows if r["Position"] == "before Decoders"]
        want += ["Decoders", "Encoders"]
        want += [r["Include"] for r in rows if r["Position"] == "after Encoders"]
        self.assertEqual(children, want)

    def test_device_mk_copies(self):
        text = (ROOT / "device.mk").read_text()
        copies = {dst: src for src, dst in
                  re.findall(r"^\s*([^\s:]+):(system/etc/[^\s\\]+)", text, re.M)}
        for name in ("media_codecs.xml", "media_profiles.xml"):
            self.assertEqual(copies.get("system/etc/" + name), "device/glass/glass-1/" + name)
        for row in table("Software codec lists (6.0)"):
            href = row["Include"]
            self.assertEqual(copies.get("system/etc/" + href), GOOGLE_DATA + href)


class MediaProfilesTest(unittest.TestCase):
    def setUp(self):
        self.root = xml_root("media_profiles.xml")
        self.assertEqual(self.root.tag, "MediaSettings")

    def test_camcorder_profiles(self):
        cams = self.root.findall("CamcorderProfiles")
        self.assertEqual([c.get("cameraId") for c in cams], ["0"])
        got = []
        for p in cams[0].findall("EncoderProfile"):
            self.assertEqual((p.get("fileFormat"), p.get("duration")), ("mp4", "30"))
            video, audio = p.find("Video"), p.find("Audio")
            self.assertEqual((video.get("codec"), audio.get("codec")), ("h264", "aac"))
            got.append({
                "Quality": p.get("quality"),
                "Video bit rate": video.get("bitRate"),
                "Width": video.get("width"),
                "Height": video.get("height"),
                "Frame rate": video.get("frameRate"),
                "Audio bit rate": audio.get("bitRate"),
                "Sample rate": audio.get("sampleRate"),
                "Channels": audio.get("channels"),
            })
        self.assertEqual(got, table("Camcorder profiles"))

    def test_image_settings(self):
        cam = self.root.find("CamcorderProfiles")
        got = []
        for e in cam:
            if e.tag == "ImageEncoding":
                got.append({"Element": "ImageEncoding quality", "Value": e.get("quality")})
            elif e.tag == "ImageDecoding":
                got.append({"Element": "ImageDecoding memCap", "Value": e.get("memCap")})
            else:
                self.assertEqual(e.tag, "EncoderProfile")
        self.assertEqual(got, table("Image settings"))

    def test_media_settings(self):
        got = [(e.tag, dict(e.attrib)) for e in self.root if e.tag != "CamcorderProfiles"]
        want = [(r["Element"], attrs(r["Attributes"])) for r in table("Media settings")]
        self.assertEqual(got, want)


if __name__ == "__main__":
    unittest.main()

# SPDX-License-Identifier: Apache-2.0
"""Check media_codecs.xml, media_profiles.xml and device.mk against docs/media-facts.md (4.4)."""
import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FACTS = ROOT / "docs" / "media-facts.md"
# AOSP 4.4 SoftOMXPlugin.cpp registers the VP8 decoder under its 4.4 name.
SOFTWARE_RENAMES = {"OMX.google.vpx.decoder": "OMX.google.vp8.decoder"}


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

    def codecs(self):
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
        return got

    def test_codecs(self):
        """4.4 has no Include: each section lists its hardware codecs, then its software codecs."""
        hardware = [(r["Kind"], r["Name"], split(r["Types"]), split(r["Quirks"]))
                    for r in table("Hardware codecs")]
        software = [(r["Kind"], SOFTWARE_RENAMES.get(r["Name"], r["Name"]), [r["Type"]], [])
                    for r in table("XE24 software codecs (for a 4.4 branch)")]
        want = [c for kind in ("decoder", "encoder")
                for c in hardware + software if c[0] == kind]
        self.assertEqual(self.codecs(), want)

    def test_no_includes(self):
        self.assertEqual([c.tag for c in self.root], ["Decoders", "Encoders"])

    def test_device_mk_copies(self):
        text = (ROOT / "device.mk").read_text()
        copies = {dst: src for src, dst in
                  re.findall(r"^\s*([^\s:]+):(system/etc/[^\s\\]+)", text, re.M)}
        for name in ("media_codecs.xml", "media_profiles.xml"):
            self.assertEqual(copies.get("system/etc/" + name), "device/glass/glass-1/" + name)
        self.assertNotIn("media_codecs_google", text)


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

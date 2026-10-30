"""SIMANIM reader and local-pose semantics, no Blender dependency."""
import struct
import tempfile
from pathlib import Path
import unittest

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import legacy
import simanim


def string(value):
    raw = value.encode()
    return struct.pack("<I", len(raw)) + raw


def artifact():
    metadata = (struct.pack("<i6fiiII3f", 3, *([0.] * 6), -1, 0, 0, 1, 0., 0., 0.)
                + struct.pack("<12f2fii", *([0.] * 14), 0, 0)
                + b"".join(string("") for _ in range(7))
                + struct.pack("<3f12fi", *([0.] * 15), 0))
    poses = []
    for name in simanim.BODY_PARTS:
        # Non-root positions must NOT replace player.object offsets.
        pos = (1., 2., 3.) if name == "player" else (99., 99., 99.)
        poses.append(struct.pack("<7f", 0., 0., 0., 1., *pos))
    return (b"SIMANIM1" + struct.pack("<II", 7, 1) + string("example")
            + struct.pack("<I3f", 1, 1., 2., 0.) + b"".join(poses) + metadata
            + struct.pack("<IIi3f", 0, 1, 0, 4., 5., 6.))


class SimanimTest(unittest.TestCase):
    def load_bytes(self, content):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.simanim"
            path.write_bytes(content)
            return simanim.load(path)

    def test_layout_and_metadata(self):
        clip, = self.load_bytes(artifact())
        self.assertEqual((clip.id, clip.name, len(clip.poses)), (0, "example", 1))
        self.assertEqual(clip.roots, [(1., 2., 0.)])
        self.assertEqual(clip.metadata["action_type"], 3)
        self.assertTrue(clip.metadata["base_animation"])
        self.assertEqual(clip.metadata["touch_frame"], -1)
        self.assertIsNone(clip.contact)
        self.assertEqual(clip.touches, [{"frame": 0, "position": (4., 5., 6.)}])

    def test_reject_corrupt_input(self):
        raw = artifact()
        for content in (b"", raw[:10], raw[:-1], raw + b"junk",
                        raw[:8] + struct.pack("<I", 8) + raw[12:]):
            with self.subTest(size=len(content)), self.assertRaises(ValueError):
                self.load_bytes(content)

    def test_local_offsets_and_full_root_height(self):
        clip, = self.load_bytes(artifact())
        joints = [legacy.Joint("player", None, (0, 0, 0), legacy.IDENTITY),
                  legacy.Joint("middle", "player", (0, 0, 0.8), legacy.IDENTITY)]
        pose = clip.world_pose(joints, 0)
        self.assertEqual(pose["player"][0], (1., 2., 3.))
        self.assertEqual(pose["middle"][0], (1., 2., 3.8))

    def test_checked_in_library(self):
        repo = Path(__file__).resolve().parents[2]
        clips = simanim.load(repo / "assets/animations/manifest.json")
        self.assertGreater(len(clips), 1000)
        joints = legacy.load_rig(repo / "data/media/objects/players/player.object")
        self.assertEqual({j.name for j in joints}, set(simanim.BODY_PARTS))
        for index, clip in enumerate(clips):
            self.assertEqual(clip.id, index)
            self.assertEqual(len(clip.roots), len(clip.poses))
            self.assertEqual(len(clip.world_pose(joints, 0)), 14)


if __name__ == "__main__":
    unittest.main(argv=[__file__])

"""Hair source import and attachment assumptions, stdlib only."""
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import legacy

ASSETS = Path(__file__).resolve().parents[2] / "data/media/objects/players"


class HairAssetsTest(unittest.TestCase):
    def test_all_style_meshes_have_valid_geometry_and_uvs(self):
        for style in ("short01", "short02", "medium01", "medium02", "long01", "long02"):
            with self.subTest(style=style):
                _, meshes = legacy.load_ase(ASSETS / "hairstyles" / (style + ".ase"))
                self.assertEqual(len(meshes), 1)
                mesh = meshes[0]
                self.assertGreater(len(mesh.faces), 0)
                self.assertTrue(all(math.isfinite(x) for p in mesh.vertices for x in p))
                for index, face in enumerate(mesh.faces):
                    self.assertTrue(all(0 <= v < len(mesh.vertices) for v in face))
                    self.assertTrue(all(int(i) in mesh.uv_vertices for i in mesh.uv_faces[index]))
                # Source vertices are head-local, not full character/world height.
                self.assertLess(max(p[2] for p in mesh.vertices), 0.5)
                self.assertGreater(max(p[2] for p in mesh.vertices), 0.15)

    def test_legacy_attachment_and_available_colors(self):
        joints = legacy.load_rig(ASSETS / "player.object")
        # Historical HumanoidBase::UpdateFullbodyNodes uses joints[2], excluding player.
        self.assertEqual(joints[3].name, "neck")
        for color in ("black", "brown", "blonde", "darkblonde", "red"):
            with self.subTest(color=color):
                content = (ASSETS / "textures/hair" / (color + ".bmp")).read_bytes()
                self.assertEqual(content[:2], b"BM")


if __name__ == "__main__":
    unittest.main(argv=[__file__])

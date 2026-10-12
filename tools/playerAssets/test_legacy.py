"""Stdlib unit tests; run with python3 -m unittest discover -s tools/playerAssets."""
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import legacy

DATA = Path(__file__).resolve().parents[2] / "data"


class WeightTests(unittest.TestCase):
    def test_known_ase_primary_weight(self):
        # Right sole uses byte 129 -> joint 12, weight 9/9.
        self.assertEqual(legacy.decode_weights((0.506, 0, 0), 13), ((12, 1.0),))

    def test_mixed_weights_and_duplicate_ids(self):
        weights = dict(legacy.decode_weights((74 / 255, 85 / 255, 0), 13))
        self.assertAlmostEqual(weights[7], 4 / 9)
        self.assertAlmostEqual(weights[8], 5 / 9)
        self.assertEqual(legacy.decode_weights((74 / 255, 75 / 255, 0), 13), ((7, 1.0),))

    def test_half_up_byte_rounding(self):
        self.assertEqual(legacy.decode_weights((128.5 / 255, 0, 0), 13), ((12, 1.0),))

    def test_invalid_weight_rejected(self):
        for color in ((0, 0, 0), (139 / 255, 0, 0), (-1, 0, 0), (1.1, 0, 0)):
            with self.subTest(color=color), self.assertRaises(ValueError):
                legacy.decode_weights(color, 13)

    def test_global_first_color_wins(self):
        def mesh(color):
            return legacy.Mesh("test", 0, [(0, 0, 0)] * 3, [(0, 1, 2)], {}, {},
                               {0: color}, {0: (0, 0, 0)}, [])
        weights, conflicts = legacy.weight_map([mesh((9 / 255, 0, 0)),
                                               mesh((19 / 255, 0, 0))], 13)
        self.assertEqual(weights[(0, 0, 0)], ((0, 1.0),))
        self.assertEqual(conflicts, 3)


class MathTests(unittest.TestCase):
    def assertVectorAlmostEqual(self, a, b):
        for x, y in zip(a, b):
            self.assertAlmostEqual(x, y, places=6)

    def test_rotation_and_shortest_slerp(self):
        q = (0, 0, math.sqrt(0.5), math.sqrt(0.5))
        self.assertVectorAlmostEqual(legacy.rotate(q, (1, 0, 0)), (0, 1, 0))
        self.assertVectorAlmostEqual(legacy.slerp(q, tuple(-x for x in q), 0.5), q)

    def test_unpose_and_deform_analytical(self):
        # Original base pose rotates the X-axis to Y; straight pose removes it.
        q = (0, 0, math.sqrt(0.5), math.sqrt(0.5))
        base = {"joint": ((1, 2, 3), q)}
        rest = {"joint": ((0, 0, 0), legacy.IDENTITY)}
        unposed = legacy.unpose_vertex((1, 3, 3), ((0, 1),), ["joint"], base, rest)
        self.assertVectorAlmostEqual(unposed, (1, 0, 0))
        posed = legacy.deform_vertex(unposed, ((0, 1),), ["joint"], rest, base)
        self.assertVectorAlmostEqual(posed, (1, 3, 3))

    def test_root_translation_not_applied_twice(self):
        joints = [legacy.Joint("player", None, (0, 0, 0), legacy.IDENTITY),
                  legacy.Joint("body", "player", (0, 0, 1), legacy.IDENTITY)]
        tracks = {"player": [(0, (2, 3, -0.1))], "body": [(0, legacy.IDENTITY)]}
        pose = legacy.evaluate_pose(joints, tracks, 0)
        self.assertVectorAlmostEqual(pose["body"][0], (2, 3, 0.9))

    def test_track_interpolation_and_clamping(self):
        keys = [(0, (0, 0, 0)), (10, (2, 4, 6))]
        self.assertEqual(legacy.sample_track(keys, 5), (1, 2, 3))
        self.assertEqual(legacy.sample_track(keys, -1), keys[0][1])
        self.assertEqual(legacy.sample_track(keys, 20), keys[-1][1])


class SourceTests(unittest.TestCase):
    def test_rig_order_and_angle_axis(self):
        joints = legacy.load_rig(DATA / "media/objects/players/player.object")
        self.assertEqual([j.name for j in joints], ["player", "body", "middle", "neck",
            "left_shoulder", "left_elbow", "right_shoulder", "right_elbow",
            "left_thigh", "left_knee", "left_ankle", "right_thigh", "right_knee", "right_ankle"])
        self.assertEqual(joints[1].rotation, legacy.IDENTITY)
        self.assertAlmostEqual(joints[4].rotation[1], -math.sin(math.radians(5)))
        self.assertEqual(joints[10].parent, "left_knee")

    def test_mesh_subset_and_all_weights(self):
        materials, meshes = legacy.load_ase(DATA / "media/objects/players/models/fullbody.ase")
        self.assertEqual(len(meshes), 12)
        self.assertEqual(sum(len(m.vertices) for m in meshes), 453)
        self.assertEqual(sum(len(m.faces) for m in meshes), 747)
        self.assertEqual({materials[m.material] for m in meshes},
                         {"shoe", "knee", "shoe_sole", "arm", "skin", "kit"})
        weights, conflicts = legacy.weight_map(meshes, 13)
        self.assertEqual(conflicts, 2)
        for influences in weights.values():
            self.assertAlmostEqual(sum(w for _, w in influences), 1)
            self.assertLessEqual(len(influences), 3)
        # ASE positions are world-space already; do not apply NODE_TM again.
        self.assertEqual(meshes[0].vertices[0], (-0.178, 0.140, 0.200))

    def test_straight_pose_replaces_object_rotations(self):
        joints = legacy.load_rig(DATA / "media/objects/players/player.object")
        tracks, _ = legacy.load_animation(DATA / "media/animations/straight.anim.util")
        pose = legacy.evaluate_pose(joints, tracks, 0)
        self.assertEqual(pose["left_shoulder"][1], legacy.IDENTITY)
        for actual, expected in zip(pose["left_ankle"][0], (0.087, -0.04, 0.09)):
            self.assertAlmostEqual(actual, expected)

    def test_walk_source_duration(self):
        tracks, metadata = legacy.load_animation(DATA / "media/animations/templates/walkD000.anim")
        self.assertEqual(len(tracks), 14)
        self.assertEqual(max(keys[-1][0] for keys in tracks.values()), 24)
        self.assertIn("movement", metadata)

    def test_bad_animation_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "bad.anim"
            for text in ("player,0,0,0\n", "player,10,0,0,0,0,0,0,0\n", "body,0,0,0,0,1\n"):
                path.write_text(text)
                with self.subTest(text=text), self.assertRaises(ValueError):
                    legacy.load_animation(path)

    def test_brace_parser_respects_quotes(self):
        self.assertEqual(list(legacy.blocks('*TEST { *NAME "a}b" *INNER { x } }', "TEST")),
                         [' *NAME "a}b" *INNER { x } '])
        with self.assertRaises(ValueError):
            list(legacy.blocks("*TEST {", "TEST"))


if __name__ == "__main__":
    result = unittest.main(argv=[__file__], exit=False).result
    if not result.wasSuccessful():
        raise AssertionError("Player asset importer tests failed")

"""Canonical GLB -> runtime cache tests; no Blender, legacy assets or compiler."""
import copy
import json
import math
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import animation_assets as assets
import simanim

REPO = Path(__file__).resolve().parents[2]
MANIFEST = REPO / "assets/animations/player_14/manifest.json"


class CatalogTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.manifest = root / "assets/animations/player_14/manifest.json"
        self.manifest.parent.mkdir(parents=True)
        self.doc = assets.json_read(MANIFEST)
        self.doc["clips"] = [copy.deepcopy(self.doc["clips"][0])]
        self.entry = self.doc["clips"][0]
        self.glb = self.manifest.parent / self.entry["file"]
        shutil.copyfile(MANIFEST.parent / self.entry["file"], self.glb)
        skeleton = root / "assets/skeletons/player_14.json"
        skeleton.parent.mkdir(parents=True)
        shutil.copyfile(REPO / "assets/skeletons/player_14.json", skeleton)
        self.save_manifest()

    def save_manifest(self):
        assets.json_write(self.manifest, self.doc)

    def alter_glb(self, operation):
        doc, binary = assets.read_glb(self.glb)
        binary = bytearray(binary)
        operation(doc, binary)
        assets.write_glb(self.glb, doc, bytes(binary))

    def test_cache_reloads_all_fields(self):
        clips = assets.load(self.manifest)
        path = Path(self.directory.name) / "runtime.simanim"
        assets.bake(self.manifest, path)
        self.assertEqual(clips, simanim.load(path))
        self.assertEqual(path.read_bytes(), assets.serialize(clips))
        self.assertEqual(json.loads(json.dumps(clips[0].metadata)), self.entry["metadata"])

    def test_glb_edit_drives_cache_without_a_legacy_fallback(self):
        original = assets.load(self.manifest)[0]
        def edit(doc, binary):
            channel = next(c for c in doc["animations"][0]["channels"]
                           if doc["nodes"][c["target"]["node"]]["name"] == "body")
            sampler = doc["animations"][0]["samplers"][channel["sampler"]]
            spec = doc["accessors"][sampler["output"]]
            view = doc["bufferViews"][spec["bufferView"]]
            q = assets.yup((0, 0, math.sin(0.2), math.cos(0.2)))
            struct.pack_into("<4f", binary, view.get("byteOffset", 0) + spec.get("byteOffset", 0), *q)
        self.alter_glb(edit)
        edited = assets.load(self.manifest)[0]
        self.assertNotEqual(original.poses[0][12], edited.poses[0][12])
        self.assertEqual(original.metadata, edited.metadata)
        self.assertEqual(original.roots, edited.roots)
        self.assertNotEqual(assets.serialize([original]), assets.serialize([edited]))

    def test_manifest_rejects_missing_metadata_and_reordered_ids(self):
        del self.entry["metadata"]["touch_frame"]
        self.save_manifest()
        with self.assertRaises(ValueError):
            assets.load(self.manifest)
        self.entry["id"] = 7
        self.save_manifest()
        with self.assertRaises(ValueError):
            assets.load(self.manifest)

    def test_no_path_escape(self):
        self.entry["file"] = "../../../outside.glb"
        self.save_manifest()
        with self.assertRaises(ValueError):
            assets.load(self.manifest)

    def test_rejects_wrong_timebase(self):
        def edit(doc, binary):
            sampler = doc["animations"][0]["samplers"][0]
            spec = doc["accessors"][sampler["input"]]
            view = doc["bufferViews"][spec["bufferView"]]
            struct.pack_into("<f", binary, view.get("byteOffset", 0) + 4, 0.02)
        self.alter_glb(edit)
        with self.assertRaisesRegex(ValueError, "100 Hz"):
            assets.load(self.manifest)

    def test_rejects_wrong_rest_basis(self):
        self.alter_glb(lambda doc, binary: doc["nodes"][0].update(rotation=[0, 1, 0, 0]))
        with self.assertRaisesRegex(ValueError, "rest basis"):
            assets.load(self.manifest)

    def test_rejects_accessor_overrun_and_preserves_previous_cache(self):
        path = Path(self.directory.name) / "cache.simanim"
        assets.bake(self.manifest, path)
        previous = path.read_bytes()
        self.alter_glb(lambda doc, binary: doc["accessors"][0].update(count=1000000))
        # Mutate an actual animation output as well; accessor 0 is inverse binds.
        def edit(doc, binary):
            spec = doc["accessors"][doc["animations"][0]["samplers"][0]["output"]]
            spec["count"] = 1000000
        self.alter_glb(edit)
        with self.assertRaises(ValueError):
            assets.bake(self.manifest, path)
        self.assertEqual(path.read_bytes(), previous)

    def test_rejects_nonfinite_and_missing_tracks(self):
        def edit(doc, binary):
            spec = doc["accessors"][doc["animations"][0]["samplers"][0]["output"]]
            offset = doc["bufferViews"][spec["bufferView"]].get("byteOffset", 0)
            struct.pack_into("<f", binary, offset, float("nan"))
        self.alter_glb(edit)
        with self.assertRaises(ValueError):
            assets.load(self.manifest)
        self.alter_glb(lambda doc, binary: doc["animations"][0]["channels"].clear())
        with self.assertRaises(ValueError):
            assets.load(self.manifest)

    def test_full_catalog_cache_roundtrip(self):
        clips = assets.load(MANIFEST)
        entries = assets.json_read(MANIFEST)["clips"]
        self.assertEqual(len(clips), len(entries))
        self.assertEqual(sum(len(c.poses) for c in clips), sum(e["frame_count"] for e in entries))
        self.assertTrue(any(c.touches for c in clips))
        path = Path(self.directory.name) / "all.simanim"
        path.write_bytes(assets.serialize(clips))
        self.assertEqual(clips, simanim.load(path))
        self.assertEqual([c.id for c in clips], list(range(len(clips))))

    def test_characters_use_shared_logical_skeleton(self):
        skeleton = assets.read_skeleton(REPO / "assets/skeletons/player_14.json")
        for name in ("body.glb", "body_low.glb", "body_smooth.glb"):
            with self.subTest(character=name):
                doc, _ = assets.read_glb(REPO / "assets/characters/default" / name)
                self.assertFalse(doc.get("animations"))
                self.assertEqual(len(doc["skins"]), 1)
                skin = doc["skins"][0]
                nodes = {doc["nodes"][i]["name"]: doc["nodes"][i] for i in skin["joints"]}
                self.assertEqual(set(nodes), set(simanim.BODY_PARTS))
                for bone in skeleton["bones"]:
                    self.assertEqual(tuple(nodes[bone["name"]]["translation"]), assets.yup(bone["offset_zup"]))
                    self.assertNotIn("rotation", nodes[bone["name"]])
                self.assertTrue(all("bufferView" in image for image in doc["images"]))


if __name__ == "__main__":
    unittest.main(argv=[__file__])

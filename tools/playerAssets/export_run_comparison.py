"""Side-by-side synchronized 14/20-bone in-place running, without constraints."""
import argparse
import hashlib
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_player as player
import export_run_20 as run
import legacy
import simanim
import verify_glb as glb


def append_character(path, count, x):
    with bpy.data.libraries.load(str(path), link=False) as (source, target):
        target.objects = source.objects
    objects = [o for o in target.objects if o is not None]
    collection = bpy.data.collections.new(f"Runner_{count}_Bones")
    bpy.context.scene.collection.children.link(collection)
    for obj in objects:
        collection.objects.link(obj)
    rigs = [o for o in objects if o.type == "ARMATURE"]
    if len(rigs) != 1 or len(rigs[0].data.bones) != count:
        raise ValueError(f"Expected static {count}-bone template")
    rig = rigs[0]
    if rig.animation_data and rig.animation_data.action:
        raise ValueError("Expected static template without actions")
    rig.name = f"Runner_{count}_Bones"
    rig.location.x = x
    rig.show_in_front = False
    for obj in objects:
        if obj.type == "MESH":
            obj.name = f"R{count}_{obj.name}"
    return rig, objects


def add_label(count, x):
    text = bpy.data.curves.new(f"Label_{count}", "FONT")
    text.body = f"{count} BONES"
    text.align_x = "CENTER"
    text.size = 0.14
    obj = bpy.data.objects.new(text.name, text)
    bpy.context.scene.collection.objects.link(obj)
    obj.location = (x, 0, 2.08)
    obj.rotation_euler = (math.pi / 2, 0, 0)


def validate_scene(rigs, frame_end):
    common = set(rigs[14].data.bones.keys())
    maximum, root_error = 0.0, 0.0
    for frame in range(frame_end + 1):
        bpy.context.scene.frame_set(frame)
        for name in common:
            a, b = (rigs[n].pose.bones[name].matrix for n in (14, 20))
            maximum = max(maximum, max(abs(a[r][c] - b[r][c]) for r in range(4) for c in range(4)))
        for rig in rigs.values():
            root = rig.pose.bones["player"].matrix.translation
            root_error = max(root_error, abs(root.x), abs(root.y))
    if maximum > 2e-5 or root_error > 1e-6:
        raise AssertionError(f"Not an aligned in-place comparison: {maximum}, {root_error}")
    constraints = sum(len(b.constraints) for rig in rigs.values() for b in rig.pose.bones)
    if constraints or any(rig.constraints for rig in rigs.values()):
        raise AssertionError("Comparison must not use constraints")
    return {"frames_checked": frame_end + 1, "shared_14_bone_matrix_error": maximum,
            "planar_root_motion_error_m": root_error, "bone_constraint_count": constraints}


def validate_glb(path, frames):
    asset = glb.load_glb(path)
    doc, _ = asset
    if sorted(len(s["joints"]) for s in doc.get("skins", [])) != [14, 20]:
        raise AssertionError("Expected two exported skeletons (14 and 20 bones)")
    if len(doc.get("animations", [])) != 1:
        raise AssertionError("Expected one shared synchronized animation")
    targeted = {c["target"]["node"] for c in doc["animations"][0]["channels"]}
    if any(not set(s["joints"]).issubset(targeted) for s in doc["skins"]):
        raise AssertionError("Animation must target both complete skeletons")
    for action in doc["animations"]:
        for sampler in action["samplers"]:
            times = glb.accessor(asset, sampler["input"])
            if len(times) != frames or any(abs(t[0] - i / 100) > 1e-6 for i, t in enumerate(times)):
                raise AssertionError("Unsynchronized GLB timebase")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 100
    bpy.context.scene.render.fps_base = 1
    bpy.ops.import_scene.gltf(filepath=str(path), bone_heuristic="BLENDER", disable_bone_shape=True)
    rigs = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    if sorted(len(r.data.bones) for r in rigs) != [14, 20]:
        raise AssertionError("Reimport lost skeletons")
    if any(not r.animation_data or not r.animation_data.action for r in rigs):
        raise AssertionError("Reimport did not activate both actions")
    scene = bpy.context.scene
    samples = []
    for frame in (0, 12):
        scene.frame_set(frame)
        samples.append({len(r.data.bones): r.pose.bones["left_knee"].matrix.translation.copy()
                        for r in rigs})
    movement = {str(n): (samples[1][n] - samples[0][n]).length for n in (14, 20)}
    if any(value < 0.05 for value in movement.values()):
        raise AssertionError("Both runners must animate on reimport")
    return {"skin_bone_counts": [14, 20], "animations": 1,
            "blender_reimport_knee_motion_m": movement}


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--templates", type=Path, default=repo / "build/player-rig-comparison")
    parser.add_argument("--output", type=Path, default=repo / "build/player-run-comparison")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    output, templates = args.output.resolve(), args.templates.resolve()
    for protected in (repo / "data", repo / "assets", templates):
        if output == protected or protected in output.parents or output in protected.parents:
            raise ValueError("Use an independent build output directory")
    paths = [templates / f"default_{n}_bones.blend" for n in (14, 20)]
    source = repo / "assets/animations/manifest.json"
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths + [source]}
    clips = simanim.load(source)
    pair = []
    for suffix in ("/movement/sprint/000_idlelevel1.anim", "/movement/sprint/000_idlelevel1.anim_mirror"):
        matches = [c for c in clips if c.name.endswith(suffix)]
        if len(matches) != 1 or len(matches[0].poses) != 25:
            raise ValueError("Expected mirrored 25-frame straight sprint clips")
        pair.append(matches[0])
    joints = legacy.load_rig(repo / "data/media/objects/players/player.object")
    output.mkdir(parents=True, exist_ok=True)
    (output / "validation.json").unlink(missing_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.preferences.filepaths.save_version = 0
    scene = bpy.context.scene
    scene.render.fps, scene.render.fps_base = 100, 1
    scene.frame_start, scene.frame_end = 0, 240
    rigs, objects, actions = {}, [], {}
    for count, x, path in zip((14, 20), (-0.7, 0.7), paths):
        rig, character = append_character(path, count, x)
        action, _ = run.make_action(rig, joints, pair, forward=False, cycles=5)
        action.name = f"run_{count}_in_place"
        if count == 14:
            action["extra_joint_animation"] = "None; original 14-bone pose only"
        rigs[count], actions[count] = rig, action
        objects.extend(character)
        add_label(count, x)
    report = validate_scene(rigs, scene.frame_end)
    report.update({"fps": 100, "duration_seconds": 2.4, "left": "14 bones",
                   "right": "20 bones", "source_sha256": hashes,
                   "note": "Same sprint, phase and XY root removal; no IK/constraints. Extra 20-bone joints procedural."})
    scene.frame_set(0)
    player.select_only([rigs[20]])
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                space = area.spaces.active
                space.shading.type = "MATERIAL"
                space.region_3d.view_rotation = Quaternion((1, 0, 0), math.pi / 2)
                space.region_3d.view_perspective = "ORTHO"
                space.region_3d.view_location = (0, 0, 1)
                space.region_3d.view_distance = 4.5
    blend_path = output / "run_14_vs_20_in_place.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))
    # Keep static node transforms at bind pose during animated GLB export.
    for count, rig in rigs.items():
        rig.animation_data_clear()
        for bone in rig.pose.bones:
            bone.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    for count, rig in rigs.items():
        rig.animation_data_create()
        rig.animation_data.action = actions[count]
    player.select_only(objects)
    glb_path = output / "run_14_vs_20_in_place.glb"
    bpy.ops.export_scene.gltf(
        filepath=str(glb_path), export_format="GLB", use_selection=True,
        export_animations=True, export_animation_mode="ACTIVE_ACTIONS",
        export_frame_range=True, export_force_sampling=True, export_frame_step=1,
        export_optimize_animation_size=False, export_skins=True, export_def_bones=False,
        export_yup=True, export_extras=True)
    report["glb"] = validate_glb(glb_path, 241)
    for path, original in hashes.items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != original:
            raise AssertionError("Modified source template or animation library")
    player.write_json(output / "validation.json", report)
    print("RUN_COMPARISON_OK", output, report, flush=True)


if __name__ == "__main__":
    main()

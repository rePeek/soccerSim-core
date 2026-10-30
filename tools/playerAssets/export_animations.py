"""Export the complete baked runtime library as separate, compatible animated GLBs.

blender -b --factory-startup --python-exit-code 1 --python tools/playerAssets/export_animations.py
"""
import argparse
import hashlib
from pathlib import Path
import re
import sys

import bpy
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_player as player
import legacy
import simanim
import verify_glb as glb


def make_action(rig, joints, clip):
    rig.animation_data_create()
    action = bpy.data.actions.new(clip.name)
    rig.animation_data.action = action
    action["source_fps"] = 100
    action["clip_id"] = clip.id
    previous = {}
    for frame in range(len(clip.poses)):
        player.apply_pose(rig, joints, clip.world_pose(joints, frame))
        for joint in joints:
            bone = rig.pose.bones[joint.name]
            # Choose a continuous quaternion sign for glTF interpolation.
            if joint.name in previous:
                bone.rotation_quaternion.make_compatible(previous[joint.name])
            previous[joint.name] = bone.rotation_quaternion.copy()
            bone.keyframe_insert("rotation_quaternion", frame=frame, group=joint.name)
            bone.keyframe_insert("location", frame=frame, group=joint.name)
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for curve in bag.fcurves:
                    for point in curve.keyframe_points:
                        point.interpolation = "LINEAR"
    return action


def verify(path, clip, joints, rest_pose):
    """Compare actual GLB skin transforms to source at EVERY 10 ms frame."""
    asset = glb.load_glb(path)
    doc, _ = asset
    if len(doc.get("skins", [])) != 1 or len(doc.get("animations", [])) != 1:
        raise AssertionError(f"Missing skin/animation: {path}")
    skin = doc["skins"][0]
    names = [doc["nodes"][i]["name"] for i in skin["joints"]]
    if set(names) != {j.name for j in joints}:
        raise AssertionError("GLB lost bones")
    animation = doc["animations"][0]
    if animation["name"] != clip.name:
        raise AssertionError("GLB action name differs from source")
    channels = []
    for channel in animation["channels"]:
        target = channel["target"]
        sampler = animation["samplers"][channel["sampler"]]
        interpolation = sampler.get("interpolation", "LINEAR")
        if interpolation not in ("LINEAR", "STEP"):
            raise AssertionError("Unexpected interpolation")
        channels.append((doc["nodes"][target["node"]]["name"], target["path"],
                         glb.accessor(asset, sampler["input"]),
                         glb.accessor(asset, sampler["output"]), interpolation))
    duration = (len(clip.poses) - 1) / 100
    if abs(max(keys[-1][0] for _, _, keys, _, _ in channels) - duration) > 2e-6:
        raise AssertionError("GLB timebase is not 100 Hz")
    inverse_binds = [glb.matrix(v) for v in glb.accessor(asset, skin["inverseBindMatrices"])]
    # Change basis Z-up -> Y-up, including rotations.
    yup = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))
    maximum = 0.0
    for frame in range(len(clip.poses)):
        overrides = {}
        for name, prop, keys, values, interpolation in channels:
            # glTF stores float32 seconds. Snap to the actual sample at this
            # native tick rather than introduce a spurious near-endpoint SLERP.
            nearest = min(range(len(keys)), key=lambda i: abs(keys[i][0] - frame / 100))
            if abs(keys[nearest][0] - frame / 100) < 1e-6:
                value = values[nearest]
            else:
                value = glb.sample(keys, values, frame / 100, prop == "rotation", interpolation)
            overrides.setdefault(name, {})[prop] = value
        worlds = glb.world_matrices(doc, overrides)
        expected = clip.world_pose(joints, frame)
        for name, node, inverse_bind in zip(names, skin["joints"], inverse_binds):
            actual = worlds[node] @ inverse_bind
            reference = (yup @ player.transform(*expected[name])
                         @ Matrix.Translation(-player.Vector(rest_pose[name][0])) @ yup.inverted())
            maximum = max(maximum, max(abs(actual[r][c] - reference[r][c])
                                      for r in range(4) for c in range(4)))
    # Float32 Blender bone-basis conversions accumulate error on long root
    # translations; this is a transform tolerance, not bit-identical C++ math.
    if maximum > 1e-4:
        raise AssertionError(f"GLB/source skin transform mismatch: {maximum}: {path}")
    return maximum


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=repo / "assets/animations/player_14/manifest.json")
    parser.add_argument("--data", type=Path, default=repo / "data")
    parser.add_argument("--output", type=Path, default=repo / "build/player-assets/runtime-animations")
    parser.add_argument("--limit", type=int, help="Export first N clips for a smoke test only")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    output, data, source = args.output.resolve(), args.data.resolve(), args.source.resolve()
    for protected in (data, repo / "assets"):
        if output == protected or protected in output.parents:
            raise ValueError("Output must not be inside source assets/data")
    if output == source or output in source.parents:
        raise ValueError("Output must not contain the source artifact")
    if args.limit is not None and args.limit <= 0:
        raise ValueError("--limit must be positive")
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    clips = simanim.load(source)
    total = len(clips)
    if args.limit:
        clips = clips[:args.limit]
    joints = legacy.load_rig(data / "media/objects/players/player.object")
    if {j.name for j in joints} != set(simanim.BODY_PARTS):
        raise ValueError("Unsupported skeleton")
    straight, _ = legacy.load_animation(data / "media/animations/straight.anim.util")
    rest_pose = legacy.evaluate_pose(joints, straight, 0)
    if any(abs(q[3]) < 1 - 1e-6 for _, q in rest_pose.values()):
        raise ValueError("Expected identity logical rest orientations")
    output.mkdir(parents=True, exist_ok=True)
    # Never leave a stale successful manifest when a rerun fails midway.
    (output / "manifest.json").unlink(missing_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    rig = player.build_rig(joints, rest_pose)
    witness = player.make_binding_witness(rig)
    scene = bpy.context.scene
    scene.render.fps, scene.render.fps_base = 100, 1.0
    scene.frame_start = 0
    scene.unit_settings.system = "METRIC"
    entries, maximum, frames = [], 0.0, 0
    for clip in clips:
        scene.frame_end = len(clip.poses) - 1
        scene.frame_set(0)
        action = make_action(rig, joints, clip)
        slug = re.sub(r"[^A-Za-z0-9_.-]+", "_", clip.name).strip("._")[:120] or "clip"
        filename = f"{clip.id:04d}_{slug}.glb"
        # Save the skeleton in its reference pose, not the last keyed pose.
        rig.animation_data_clear()
        player.apply_pose(rig, joints, rest_pose)
        rig.animation_data_create()
        rig.animation_data.action = action
        player.select_only([rig, witness])
        bpy.ops.export_scene.gltf(
            filepath=str(output / filename), export_format="GLB", use_selection=True,
            export_animations=True, export_animation_mode="ACTIVE_ACTIONS",
            export_nla_strips_merged_animation_name=clip.name,
            export_frame_range=True, export_force_sampling=True, export_frame_step=1,
            export_optimize_animation_size=False,
            export_skins=True, export_def_bones=False, export_yup=True, export_extras=True)
        error = verify(output / filename, clip, joints, rest_pose)
        maximum = max(maximum, error)
        frames += len(clip.poses)
        entries.append({"id": clip.id, "name": clip.name, "file": filename,
                        "frame_count": len(clip.poses), "duration_seconds": scene.frame_end / 100,
                        "root_motion": "preserved_xyz", "loop": False,
                        "metadata": clip.metadata, "contact_position": clip.contact,
                        "touches": clip.touches, "root_positions_xy": clip.roots,
                        "sha256": hashlib.sha256((output / filename).read_bytes()).hexdigest()})
        rig.animation_data_clear()
        bpy.data.actions.remove(action)
        print(f"ANIMATION_OK {clip.id + 1}/{total} {filename} error={error:.8g}", flush=True)
    if hashlib.sha256(source.read_bytes()).hexdigest() != source_hash:
        raise AssertionError("Source artifact changed during export")
    player.write_json(output / "skeleton.json", {
        "rig_version": player.RIG_VERSION, "bones": [
            {"name": j.name, "parent": j.parent,
             "rest_matrix_blender": [list(row) for row in rig.data.bones[j.name].matrix_local]}
            for j in joints]})
    player.write_json(output / "manifest.json", {
        "rig_version": player.RIG_VERSION, "source": str(source), "source_sha256": source_hash,
        "fps": 100, "units": "metres", "up_axis": "Y", "source_clip_count": total,
        "complete": len(entries) == total, "clips": entries,
        "binding_witness": "__rig_binding__: transparent triangle, not character appearance",
        "validation": {"frames_checked": frames, "max_skin_matrix_error": maximum,
                       "godot_tested": False}})
    print(f"ANIMATIONS_EXPORTED {len(entries)} clips, {frames} frames, {output}", flush=True)


if __name__ == "__main__":
    main()

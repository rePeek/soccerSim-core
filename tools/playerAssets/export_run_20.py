"""Offline 20-bone running preview: legacy sprint + procedural extra joints."""
import argparse
import hashlib
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_player as player
import export_rig_comparison as comparison
import legacy
import simanim
import verify_glb as glb

ACTION = "run_20_preview"


def cycle_pose(clips, joints, frame):
    """Two mirrored 24-tick half steps; ease the small shoulder seam over 4 ticks."""
    half, tick = divmod(frame % 48, 24)
    clip, following = clips[half], clips[1 - half]
    rows = clip.poses[tick]
    tracks = {}
    for name, row, next_row in zip(simanim.BODY_PARTS, rows, following.poses[0]):
        value = row[4:] if name == "player" else legacy.normalize(row[:4])
        if name != "player" and tick >= 20:
            t = comparison.smoothstep((tick - 20) / 4)
            value = legacy.slerp(value, legacy.normalize(next_row[:4]), t)
        tracks[name] = [(0, value)]
    return legacy.evaluate_pose(joints, tracks, 0)


def set_pose(rig, joints, clips, frame, forward):
    pose = cycle_pose(clips, joints, frame)
    half = (frame % 48) // 24
    cycle = frame // 48
    half_delta = [Vector(c.poses[-1][-1][4:]) - Vector(c.poses[0][-1][4:]) for c in clips]
    current_start = Vector(clips[half].poses[0][-1][4:])
    shift = Vector((0, 0, 0))
    if forward:
        shift = (half_delta[0] + half_delta[1]) * cycle
        if half:
            shift += half_delta[0]
        shift -= current_start
    else:
        root = Vector(pose["player"][0])
        shift = Vector((-root.x, -root.y, 0))
    # Keep vertical root motion; only remove/accumulate planar locomotion.
    shift.z = 0
    pose = {name: (tuple(Vector(position) + shift), rotation)
            for name, (position, rotation) in pose.items()}
    desired = {}
    phase = 2 * math.pi * (frame % 48) / 48
    for bone in rig.data.bones:
        rest = bone.matrix_local.copy()
        if bone.name in pose:
            # Preserve the old joints' world motion even after inserting clavicles.
            # The logical rest pose has identity rotations at player.object offsets.
            target = player.transform(*pose[bone.name]) @ rest.to_3x3().to_4x4()
        else:
            local = rig.data.bones[bone.parent.name].matrix_local.inverted() @ rest
            offset = 0 if bone.name.startswith("left") else math.pi
            if bone.name.endswith("_wrist"):
                rotation = Quaternion((1, 0, 0), math.radians(8) * math.sin(phase + offset))
            elif bone.name.endswith("_toe"):
                angle = math.radians(18) * max(0, math.sin(phase + offset)) ** 4
                rotation = Quaternion((1, 0, 0), angle)
            elif bone.name.endswith("_clavicle"):
                angle = math.radians(3) * math.sin(phase + offset)
                rotation = Quaternion((0, 0, 1), angle)
            else:
                raise ValueError(f"Unknown added bone: {bone.name}")
            target = desired[bone.parent.name] @ local @ rotation.to_matrix().to_4x4()
        desired[bone.name] = target
        relative_rest, relative_target = rest, target
        if bone.parent:
            relative_rest = bone.parent.matrix_local.inverted() @ rest
            relative_target = desired[bone.parent.name].inverted() @ target
        rig.pose.bones[bone.name].rotation_mode = "QUATERNION"
        rig.pose.bones[bone.name].matrix_basis = relative_rest.inverted() @ relative_target
    bpy.context.view_layer.update()
    return desired


def make_action(rig, joints, clips, forward, cycles):
    rig.animation_data_create()
    action = bpy.data.actions.new(ACTION + ("_forward" if forward else "_in_place"))
    rig.animation_data.action = action
    action["preview_only"] = True
    action["source_fps"] = 100
    action["extra_joint_animation"] = "Procedural preview, not captured motion or foot-contact IK"
    previous, snapshots = {}, []
    for frame in range(cycles * 48 + 1):
        set_pose(rig, joints, clips, frame, forward)
        for bone in rig.pose.bones:
            if bone.name in previous:
                bone.rotation_quaternion.make_compatible(previous[bone.name])
            previous[bone.name] = bone.rotation_quaternion.copy()
            bone.keyframe_insert("rotation_quaternion", frame=frame, group=bone.name)
            bone.keyframe_insert("location", frame=frame, group=bone.name)
        snapshots.append({b.name: b.matrix.copy() for b in rig.pose.bones})
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for curve in bag.fcurves:
                    for key in curve.keyframe_points:
                        key.interpolation = "LINEAR"
    return action, snapshots


def verify_bytes(path, rig_rest, snapshots):
    asset = glb.load_glb(path)
    doc, _ = asset
    if len(doc.get("skins", [])) != 1 or len(doc.get("animations", [])) != 1:
        raise AssertionError("Missing animated skin")
    skin = doc["skins"][0]
    names = [doc["nodes"][i]["name"] for i in skin["joints"]]
    if set(names) != set(rig_rest) or len(names) != 20 or len(doc["meshes"]) != 12:
        raise AssertionError("Unexpected exported character")
    clip = doc["animations"][0]
    channels = []
    for channel in clip["channels"]:
        sampler = clip["samplers"][channel["sampler"]]
        target = channel["target"]
        keys = glb.accessor(asset, sampler["input"])
        values = glb.accessor(asset, sampler["output"])
        if len(keys) != len(snapshots) or any(abs(t[0] - i / 100) > 1e-6 for i, t in enumerate(keys)):
            raise AssertionError("Animation is not fully sampled at 100 Hz")
        channels.append((doc["nodes"][target["node"]]["name"], target["path"], values))
    binds = [glb.matrix(v) for v in glb.accessor(asset, skin["inverseBindMatrices"])]
    yup = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))
    maximum = 0.0
    for frame, reference in enumerate(snapshots):
        overrides = {}
        for name, prop, values in channels:
            overrides.setdefault(name, {})[prop] = values[frame]
        worlds = glb.world_matrices(doc, overrides)
        for name, node, inverse_bind in zip(names, skin["joints"], binds):
            actual = worlds[node] @ inverse_bind
            expected = yup @ reference[name] @ rig_rest[name].inverted() @ yup.inverted()
            maximum = max(maximum, max(abs(actual[r][c] - expected[r][c])
                                      for r in range(4) for c in range(4)))
    if maximum > 1e-4:
        raise AssertionError(f"Exported animation mismatch: {maximum}")
    return {"bones": 20, "meshes": 12, "frames_checked": len(snapshots),
            "max_skin_matrix_error": maximum}


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", type=Path, default=repo / "build/player-rig-comparison/default_20_bones.blend")
    parser.add_argument("--output", type=Path, default=repo / "build/player-run-20")
    parser.add_argument("--cycles", type=int, default=5)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    output, template = args.output.resolve(), args.template.resolve()
    if not 1 <= args.cycles <= 20:
        raise ValueError("--cycles must be between 1 and 20")
    for protected in (repo / "data", repo / "assets"):
        if output == protected or protected in output.parents:
            raise ValueError("Output must not overwrite source assets")
    if output == template or output in template.parents:
        raise ValueError("Use a separate output directory")
    source = repo / "assets/runtime/animations.simanim"
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    template_hash = hashlib.sha256(template.read_bytes()).hexdigest()
    all_clips = simanim.load(source)
    # Match semantic source names, not machine-specific absolute paths or clip IDs.
    normal = [c for c in all_clips if c.name.endswith("/movement/sprint/000_idlelevel1.anim")]
    mirrored = [c for c in all_clips if c.name.endswith("/movement/sprint/000_idlelevel1.anim_mirror")]
    if len(normal) != 1 or len(mirrored) != 1:
        raise ValueError("Expected the mirrored straight sprint half-step pair")
    clips = [normal[0], mirrored[0]]
    if any(len(c.poses) != 25 for c in clips):
        raise ValueError("Expected 25-frame half steps")
    joints = legacy.load_rig(repo / "data/media/objects/players/player.object")
    output.mkdir(parents=True, exist_ok=True)
    (output / "validation.json").unlink(missing_ok=True)
    report = {"preview_only": True, "fps": 100, "cycles": args.cycles,
              "source_sha256": source_hash, "template_sha256": template_hash,
              "source_clips": [{"id": c.id, "name": c.name} for c in clips],
              "note": "14-joint sprint retargeted to 20; new joints procedural; no foot-contact IK; no runtime changes.",
              "files": {}}
    for forward in (False, True):
        bpy.ops.wm.open_mainfile(filepath=str(template))
        bpy.context.preferences.filepaths.save_version = 0
        if bpy.context.object and bpy.context.object.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        rig = bpy.data.objects["PlayerSkeleton"]
        if len(rig.data.bones) != 20 or rig.animation_data and rig.animation_data.action:
            raise ValueError("Expected static 20-bone template")
        objects = [o for o in bpy.data.objects if o.type == "MESH"]
        rest = {b.name: b.matrix_local.copy() for b in rig.data.bones}
        action, snapshots = make_action(rig, joints, clips, forward, args.cycles)
        scene = bpy.context.scene
        scene.render.fps, scene.render.fps_base = 100, 1.0
        scene.frame_start, scene.frame_end = 0, args.cycles * 48
        scene.frame_set(0)
        basename = "run_20_forward" if forward else "run_20_in_place"
        comparison.save_scene(output / (basename + ".blend"), rig)
        # Export reference node transforms, while retaining the active action.
        rig.animation_data_clear()
        for bone in rig.pose.bones:
            bone.matrix_basis = Matrix.Identity(4)
        bpy.context.view_layer.update()
        rig.animation_data_create()
        rig.animation_data.action = action
        player.select_only([rig] + objects)
        bpy.ops.export_scene.gltf(
            filepath=str(output / (basename + ".glb")), export_format="GLB", use_selection=True,
            export_animations=True, export_animation_mode="ACTIVE_ACTIONS",
            export_nla_strips_merged_animation_name=action.name,
            export_frame_range=True, export_force_sampling=True, export_frame_step=1,
            export_optimize_animation_size=False, export_skins=True, export_def_bones=False,
            export_yup=True, export_extras=True)
        result = verify_bytes(output / (basename + ".glb"), rest, snapshots)
        result["root_motion"] = "forward_xyz" if forward else "in_place_xy_removed_z_preserved"
        result["duration_seconds"] = scene.frame_end / 100
        if not forward:
            seam = max(abs(snapshots[0][name][r][c] - snapshots[-1][name][r][c])
                       for name in rest for r in range(4) for c in range(4))
            if seam > 1e-5:
                raise AssertionError(f"In-place loop does not close: {seam}")
            result["loop_endpoint_matrix_error"] = seam
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.scene.render.fps = 100
        bpy.context.scene.render.fps_base = 1.0
        bpy.ops.import_scene.gltf(filepath=str(output / (basename + ".glb")),
                                  bone_heuristic="BLENDER", disable_bone_shape=True)
        rigs = [o for o in bpy.data.objects if o.type == "ARMATURE"]
        if len(rigs) != 1 or len(rigs[0].data.bones) != 20 or len(bpy.data.actions) != 1:
            raise AssertionError("Blender reimport lost animated rig")
        # Verify animation is active and visibly moves the skinned character.
        imported = [o for o in bpy.data.objects if o.type == "MESH"]
        bpy.context.scene.frame_set(0)
        first = comparison.evaluated_vertices(imported)
        bpy.context.scene.frame_set(12)
        second = comparison.evaluated_vertices(imported)
        movement = max((a - b).length for n in first for a, b in zip(first[n], second[n]))
        if movement < 0.05:
            raise AssertionError("Reimported animation does not deform the model")
        result["blender_reimport"] = {"bones": 20, "actions": 1,
                                       "max_vertex_motion_m": movement}
        report["files"][basename] = result
    if hashlib.sha256(source.read_bytes()).hexdigest() != source_hash or hashlib.sha256(template.read_bytes()).hexdigest() != template_hash:
        raise AssertionError("Modified input assets")
    player.write_json(output / "validation.json", report)
    print("RUN_20_OK", output, report, flush=True)


if __name__ == "__main__":
    main()

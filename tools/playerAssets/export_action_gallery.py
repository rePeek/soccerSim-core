"""Six 14-bone in-place acceleration/turn/ball-control previews in one Blender scene.

Original clips run at 0.5x; holds and pose-reset blends are presentation only.
Ball trajectories are illustrative guides anchored at the source touch, NOT physics.
"""
import argparse
import hashlib
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_player as player
import export_run_comparison as comparison
import legacy
import simanim

SPECS = (
    ("01_START_ACCEL", "START ACCEL | 0 -> 3.5 m/s", "/movement/idle/000_accel.anim"),
    ("02_RUN_ACCEL", "RUN ACCEL | 5 -> 7 m/s", "/movement/walk/000_accel.anim"),
    ("03_TURN_45", "TURN 45 DEG | 5 m/s", "/movement/walk/045.anim"),
    ("04_TURN_90", "TURN 90 DEG | 5 -> 3.5 m/s", "/movement/walk/090_left_decel.anim"),
    ("05_DRIBBLE", "BALL CONTROL | 5 m/s", "/ballcontrol/walk/000.anim"),
    ("06_DRIBBLE_FAST", "BALL CONTROL | 7 m/s", "/ballcontrol/sprint/000.anim"),
)


def sample_rows(clip, time):
    low = int(math.floor(time))
    high = min(low + 1, len(clip.poses) - 1)
    bias = time - low
    rows = []
    for a, b in zip(clip.poses[low], clip.poses[high]):
        q = legacy.slerp(legacy.normalize(a[:4]), legacy.normalize(b[:4]), bias)
        p = tuple(x * (1 - bias) + y * bias for x, y in zip(a[4:], b[4:]))
        rows.append(q + p)
    return rows


def preview_sample(clip, frame):
    tick = frame % 100
    source_time = min(max((tick - 10) / 2, 0), len(clip.poses) - 1)
    rows = sample_rows(clip, source_time)
    # Fade to the original start pose only after the actual clip has finished.
    reset = 0.0
    if tick >= 85:
        t = (tick - 85) / 15
        reset = t * t * (3 - 2 * t)
        rows = [legacy.slerp(a[:4], legacy.normalize(b[:4]), reset)
                + tuple(x * (1 - reset) + y * reset for x, y in zip(a[4:], b[4:]))
                for a, b in zip(rows, clip.poses[0])]
    return rows, source_time, reset


def pose_from_rows(joints, rows):
    tracks = {name: [(0, row[4:] if name == "player" else row[:4])]
              for name, row in zip(simanim.BODY_PARTS, rows)}
    pose = legacy.evaluate_pose(joints, tracks, 0)
    root = Vector(pose["player"][0])
    return {name: ((p[0] - root.x, p[1] - root.y, p[2]), q) for name, (p, q) in pose.items()}


def label(text, x, z, size=0.115):
    data = bpy.data.curves.new("GalleryLabel", "FONT")
    data.body, data.align_x, data.size = text, "CENTER", size
    obj = bpy.data.objects.new(text, data)
    bpy.context.scene.collection.objects.link(obj)
    obj.location = (x, -0.6, z)
    obj.rotation_euler = (math.pi / 2, 0, 0)
    return obj


def ball_material(name, color):
    material = bpy.data.materials.new(name)
    material.diffuse_color = (*color, 1)
    material.use_nodes = True
    shader = material.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1)
    shader.inputs["Roughness"].default_value = 0.7
    return material


def make_ball(name):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=0.11)
    ball = bpy.context.object
    ball.name = name + "_GUIDE_BALL_NOT_PHYSICS"
    ball.data.materials.append(ball_material("GuideBallWhite", (0.85, 0.85, 0.85)))
    ball.data.materials.append(ball_material("GuideBallDark", (0.015, 0.015, 0.015)))
    for face in ball.data.polygons:
        center = face.center
        azimuth = math.atan2(center.y, center.x)
        band = int((center.z / 0.11 + 1) * 3)
        face.material_index = int((int((azimuth + math.pi) * 3 / math.pi) + band) % 3 == 0)
        face.use_smooth = True
    ball["illustrative_only"] = True
    return ball


def guide_ball_position(clip, source_time):
    touch = clip.touches[0]
    position = Vector(touch["position"])
    speed = clip.metadata["incoming_velocity"]
    position.y -= speed * (source_time - touch["frame"]) / 100
    root = sample_rows(clip, source_time)[-1][4:]
    position.x -= root[0]
    position.y -= root[1]
    position.z = 0.11
    return position


def linear_keys(action):
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for curve in bag.fcurves:
                    for key in curve.keyframe_points:
                        key.interpolation = "LINEAR"


def animate(rig, ball, clip, joints, location):
    rig.animation_data_create()
    action = bpy.data.actions.new(rig.name + "_in_place_0.5x")
    rig.animation_data.action = action
    action["source_clip_id"], action["source_name"] = clip.id, clip.name
    action["preview_time_scale"] = 0.5
    action["note"] = "10-tick start hold; native clip at half speed; hold; 15-tick presentation reset"
    previous = {}
    max_reference_error = 0.0
    for frame in range(301):
        rows, source_time, reset = preview_sample(clip, frame)
        expected = pose_from_rows(joints, rows)
        player.apply_pose(rig, joints, expected)
        for bone in rig.pose.bones:
            error = (bone.matrix.translation - Vector(expected[bone.name][0])).length
            max_reference_error = max(max_reference_error, error)
            if bone.name in previous:
                bone.rotation_quaternion.make_compatible(previous[bone.name])
            previous[bone.name] = bone.rotation_quaternion.copy()
            bone.keyframe_insert("rotation_quaternion", frame=frame, group=bone.name)
            bone.keyframe_insert("location", frame=frame, group=bone.name)
        if ball:
            p = guide_ball_position(clip, source_time)
            p = p.lerp(guide_ball_position(clip, 0), reset)
            ball.location = Vector(location) + p
            ball.rotation_euler.x = -clip.metadata["incoming_velocity"] * source_time * (1 - reset) / 100 / 0.11
            ball.keyframe_insert("location", frame=frame)
            ball.keyframe_insert("rotation_euler", frame=frame)
    if max_reference_error > 2e-5:
        raise AssertionError(f"Retargeting changed source joints: {max_reference_error}")
    linear_keys(action)
    if ball:
        linear_keys(ball.animation_data.action)
    return {"source_id": clip.id, "source_name": clip.name, "source_frames": len(clip.poses),
            "incoming_speed_mps": clip.metadata["incoming_velocity"],
            "outgoing_speed_mps": clip.metadata["outgoing_velocity"],
            "turn_degrees": math.degrees(clip.metadata["outgoing_angle"]),
            "max_source_joint_position_error_m": max_reference_error,
            "guide_ball": bool(ball), "source_touches": clip.touches}


def validate(rigs):
    scene = bpy.context.scene
    root_error, repeat_error = 0.0, 0.0
    initial, poses = {}, {}
    movement = {rig.name: 0.0 for rig in rigs}
    for frame in range(301):
        scene.frame_set(frame)
        for rig in rigs:
            if len(rig.data.bones) != 14 or any(b.constraints for b in rig.pose.bones):
                raise AssertionError("Expected 14-bone rigs without constraints")
            root = rig.pose.bones["player"].matrix.translation
            root_error = max(root_error, abs(root.x), abs(root.y))
            snapshot = [b.matrix.copy() for b in rig.pose.bones]
            key = (rig.name, frame % 100)
            if key in poses:
                repeat_error = max(repeat_error, max(abs(a[r][c] - b[r][c])
                                   for a, b in zip(snapshot, poses[key]) for r in range(4) for c in range(4)))
            else:
                poses[key] = snapshot
            if frame == 0:
                initial[rig.name] = snapshot
            elif frame < 85:
                movement[rig.name] = max(movement[rig.name], max(
                    (a.translation - b.translation).length for a, b in zip(snapshot, initial[rig.name])))
    if root_error > 1e-6 or repeat_error > 2e-5:
        raise AssertionError(f"Invalid in-place/repeat behavior: {root_error}, {repeat_error}")
    if any(distance < 0.03 for distance in movement.values()):
        raise AssertionError(f"A character did not animate: {movement}")
    return {"characters": len(rigs), "bones_each": 14, "frames_checked": 301,
            "max_planar_root_motion_m": root_error, "repeat_matrix_error": repeat_error,
            "max_joint_motion_m": movement}


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", type=Path, default=repo / "build/player-rig-comparison/default_14_bones.blend")
    parser.add_argument("--output", type=Path, default=repo / "build/player-action-gallery")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    output, template = args.output.resolve(), args.template.resolve()
    for protected in (repo / "data", repo / "assets", template.parent):
        if output == protected or protected in output.parents or output in protected.parents:
            raise ValueError("Use an independent output directory")
    source = repo / "assets/animations/manifest.json"
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (source, template)}
    clips = simanim.load(source)
    joints = legacy.load_rig(repo / "data/media/objects/players/player.object")
    output.mkdir(parents=True, exist_ok=True)
    (output / "validation.json").unlink(missing_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.preferences.filepaths.save_version = 0
    scene = bpy.context.scene
    scene.render.fps, scene.render.fps_base = 100, 1
    scene.frame_start, scene.frame_end = 0, 299
    rigs, entries = [], []
    for index, (name, title, suffix) in enumerate(SPECS):
        matches = [c for c in clips if c.name.endswith(suffix)]
        if len(matches) != 1:
            raise ValueError(f"Expected one source clip: {suffix}")
        clip = matches[0]
        if (len(clip.poses) - 1) * 2 + 10 > 85:
            raise ValueError("Clip would overlap the reset interval")
        x, z = (index % 3 - 1) * 2.0, 2.6 if index < 3 else 0.0
        rig, objects = comparison.append_character(template, 14, x)
        rig.name, rig.location.z = name, z
        rigs.append(rig)
        ball = make_ball(name) if clip.touches else None
        entries.append(animate(rig, ball, clip, joints, (x, 0, z)))
        label(title, x, z + 2.02, 0.105)
        label("14 BONES | 0.5x" + (" | GUIDE BALL" if ball else ""), x, z + 1.88, 0.09)
    label("14-BONE ACTION GALLERY", 0, 5.12, 0.23)
    label("HALF SPEED | native clips + holds + RESET BLEND | guide balls are NOT simulated", 0, -0.38, 0.105)
    for cycle in range(3):
        for offset, title in ((0, "START HOLD"), (10, "SOURCE ACTION 0.5x"), (85, "PREVIEW RESET")):
            scene.timeline_markers.new(title, frame=cycle * 100 + offset)
    report = validate(rigs)
    report.update({"fps": 100, "scene_seconds": 3, "playback_speed": 0.5,
                   "source_sha256": hashes, "clips": entries,
                   "note": "XY root translation removed; turns retained. Presentation reset is not a game transition. Ball guides are touch-anchored linear illustrations, not simulation."})
    scene.frame_set(20)
    player.select_only([rigs[0]])
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                space = area.spaces.active
                space.shading.type = "MATERIAL"
                space.region_3d.view_rotation = Quaternion((1, 0, 0), math.pi / 2)
                space.region_3d.view_perspective = "ORTHO"
                space.region_3d.view_location = (0, 0, 2.35)
                space.region_3d.view_distance = 8.5
    path = output / "actions_14_in_place.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(path))
    # Reopen the actual saved artifact, not just the in-memory construction.
    bpy.ops.wm.open_mainfile(filepath=str(path))
    loaded = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    report["saved_blend_reopen"] = validate(loaded)
    for p, original in hashes.items():
        if hashlib.sha256(Path(p).read_bytes()).hexdigest() != original:
            raise AssertionError("Modified source assets")
    player.write_json(output / "validation.json", report)
    print("ACTION_GALLERY_OK", path, report, flush=True)


if __name__ == "__main__":
    main()

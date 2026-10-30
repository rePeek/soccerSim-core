"""One Blender scene containing every manual ball-carrying turn/feint clip.

The legacy library has no stepover/elastico/rabona set: its 1v1 repertoire is
180-degree cuts, hold-ball spins, the Zidane turn and a chest-control turn.
Previews run at half speed with a presentation reset; nothing is simulated.
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

CYCLE = 400
HOLD = 10
RESET = 20
CLIPS = (
    ("ZIDANE TURN", "/shot/dribble/090_zidane.anim"),
    ("PASS 180 TURN", "/pass/dribble/180_turn.anim"),
    ("SHOT 180 TURN", "/shot/walk/180_turn.anim"),
    ("HIGH PASS 180 TURN", "/highpass/walk/180_turn.anim"),
    ("DRIBBLE TURN 180 IDLE", "/ballcontrol/idle/180.anim"),
    ("DRIBBLE TURN 180 ACCEL", "/ballcontrol/idle/180_accel.anim"),
    ("DRIBBLE TURN 180 DECEL", "/ballcontrol/walk/180_decel.anim"),
    ("DRIBBLE 180 CUT", "/ballcontrol/walk/180_decel_decel.anim"),
    ("SPRINT 180 CUT", "/ballcontrol/sprint/180_decel_decel_decel.anim"),
    ("TURN 180 ACCEL", "/movement/idle/180_accel.anim"),
    ("DIAGONAL 180 ACCEL", "/movement/idle/angled/D045_180_accel.anim"),
    ("DRIBBLE 180 DECEL", "/movement/dribble/180_decel.anim"),
    ("BACK TO FRONT SPIN", "/movement_special/idle/special/000_back_to_front_holdball.anim"),
    ("HOLD BALL 180 SPIN", "/movement_special/dribble/angled/special/D000_to_D135_180_holdball.anim"),
    ("HOLD BALL TURN BACK", "/movement_special/dribble/angled/special/D135_to_D000_holdball.anim"),
    ("HOLD BALL 135 DECEL", "/movement_special/dribble/angled/special/D135_decel_holdball.anim"),
    ("HOLD BALL 135 LEFT", "/movement_special/dribble/angled/special/D135_left_holdball.anim"),
    ("HOLD BALL 90 DECEL", "/movement_special/dribble/special/090_decel_holdball.anim"),
    ("HOLD BALL 45", "/movement_special/idle/special/045_holdball.anim"),
    ("HOLD BALL 135 ACCEL", "/movement_special/idle/special/135_accel_holdball.anim"),
    ("CHEST 180 CONTROL", "/trap/walk/highballs/D135_chest_jump_180.anim"),
)


def sample_rows(clip, time):
    low = int(math.floor(time))
    high = min(low + 1, len(clip.poses) - 1)
    bias = time - low
    return [legacy.slerp(legacy.normalize(a[:4]), legacy.normalize(b[:4]), bias)
            + tuple(x * (1 - bias) + y * bias for x, y in zip(a[4:], b[4:]))
            for a, b in zip(clip.poses[low], clip.poses[high])]


def timeline(clip, frame):
    """Half-speed source playback, then a presentation reset (not a game blend)."""
    local = frame % CYCLE
    last = len(clip.poses) - 1
    source = min(max((local - HOLD) / 2, 0), last)
    rows = sample_rows(clip, source)
    reset = 0.0
    if local >= CYCLE - RESET:
        t = (local - (CYCLE - RESET)) / RESET
        reset = t * t * (3 - 2 * t)
        start = clip.poses[0]
        rows = [legacy.slerp(a[:4], legacy.normalize(b[:4]), reset)
                + tuple(x * (1 - reset) + y * reset for x, y in zip(a[4:], b[4:]))
                for a, b in zip(rows, start)]
    return rows, source, reset


def pose_from_rows(joints, rows):
    tracks = {name: [(0, row[4:] if name == "player" else row[:4])]
              for name, row in zip(simanim.BODY_PARTS, rows)}
    pose = legacy.evaluate_pose(joints, tracks, 0)
    root = Vector(pose["player"][0])
    return {name: ((p[0] - root.x, p[1] - root.y, p[2]), q)
            for name, (p, q) in pose.items()}


def label(text, x, z, size=0.075):
    data = bpy.data.curves.new("SkillLabel", "FONT")
    data.body, data.align_x, data.size = text, "CENTER", size
    obj = bpy.data.objects.new("Label " + text, data)
    bpy.context.scene.collection.objects.link(obj)
    obj.location = (x, -0.5, z - 0.34)
    obj.rotation_euler = (math.pi / 2, 0, 0)


def animate(rig, joints, clip, location):
    rig.animation_data_create()
    action = bpy.data.actions.new(rig.name + "_skill")
    rig.animation_data.action = action
    action["source_clip_id"] = clip.id
    action["source_name"] = clip.name
    action["preview_time_scale"] = 0.5
    action["preview_cycle_frames"] = CYCLE
    previous, error = {}, 0.0
    for frame in range(CYCLE):
        rows, _, _ = timeline(clip, frame)
        expected = pose_from_rows(joints, rows)
        player.apply_pose(rig, joints, expected)
        for bone in rig.pose.bones:
            error = max(error, (bone.matrix.translation - Vector(expected[bone.name][0])).length)
            if bone.name in previous:
                bone.rotation_quaternion.make_compatible(previous[bone.name])
            previous[bone.name] = bone.rotation_quaternion.copy()
            bone.keyframe_insert("rotation_quaternion", frame=frame, group=bone.name)
            bone.keyframe_insert("location", frame=frame, group=bone.name)
        rig.location = location
        rig.keyframe_insert("location", frame=frame)
    if error > 2e-5:
        raise AssertionError(f"Retargeting changed source joints: {error}")
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for curve in bag.fcurves:
                    for key in curve.keyframe_points:
                        key.interpolation = "LINEAR"
    return {"id": clip.id, "source": clip.name, "source_frames": len(clip.poses),
            "turn_degrees": math.degrees(clip.metadata["outgoing_angle"]),
            "incoming_speed_mps": clip.metadata["incoming_velocity"],
            "outgoing_speed_mps": clip.metadata["outgoing_velocity"],
            "touch_frame": clip.metadata["touch_frame"],
            "max_source_joint_error_m": error}


def validate(rigs):
    scene = bpy.context.scene
    root_error, poses = 0.0, {}
    motion = {rig.name: 0.0 for rig in rigs}
    initial = {}
    for frame in range(CYCLE):
        scene.frame_set(frame)
        for rig in rigs:
            if len(rig.data.bones) != 14 or any(b.constraints for b in rig.pose.bones):
                raise AssertionError("Expected unconstrained 14-bone rigs")
            root = rig.pose.bones["player"].matrix.translation
            root_error = max(root_error, abs(root.x), abs(root.y))
            snapshot = [b.matrix.copy() for b in rig.pose.bones]
            key = (rig.name, frame)
            poses[key] = snapshot
            if frame == 0:
                initial[rig.name] = snapshot
            else:
                motion[rig.name] = max(motion[rig.name], max(
                    (a.translation - b.translation).length
                    for a, b in zip(snapshot, initial[rig.name])))
    # Cycle is exactly repeatable: frame 0 == frame CYCLE is implied by modulo.
    if root_error > 1e-6 or any(distance < 0.03 for distance in motion.values()):
        raise AssertionError(f"Invalid in-place/motion: {root_error}, {motion}")
    return {"rigs": len(rigs), "frames_checked": CYCLE,
            "max_planar_root_motion_m": root_error, "max_joint_motion_m": motion}


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", type=Path,
                        default=repo / "build/player-smooth/default_14_smooth.blend")
    parser.add_argument("--output", type=Path, default=repo / "build/player-skill-gallery")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    output, template = args.output.resolve(), args.template.resolve()
    for protected in (repo / "data", repo / "assets", template.parent):
        if output == protected or output in protected.parents or output in protected.parents:
            raise ValueError("Use an independent output directory")
    catalog = repo / "assets/animations/manifest.json"
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (template, catalog)}
    clips = simanim.load(catalog)
    joints = legacy.load_rig(repo / "data/media/objects/players/player.object")
    chosen = []
    for title, suffix in CLIPS:
        matches = [c for c in clips if c.name.endswith(suffix)]
        if len(matches) != 1:
            raise ValueError(f"Expected one source clip: {suffix}")
        chosen.append((title, matches[0], suffix))
    output.mkdir(parents=True, exist_ok=True)
    (output / "validation.json").unlink(missing_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.preferences.filepaths.save_version = 0
    scene = bpy.context.scene
    scene.render.fps, scene.render.fps_base = 100, 1
    scene.frame_start, scene.frame_end = 0, CYCLE - 1
    rigs, entries = [], []
    for index, (title, clip, suffix) in enumerate(chosen):
        row, column = divmod(index, 3)
        x, z = (column - 1) * 1.6, -row * 2.0
        rig, _ = comparison.append_character(template, 14, x)
        rig.name, rig.location.z = f"Skill_{index:02d}", z
        rigs.append(rig)
        entries.append(animate(rig, joints, clip, (x, 0, z)))
        entries[-1]["title"] = title
        if 2 * (len(clip.poses) - 1) + HOLD + RESET + 10 > CYCLE:
            raise ValueError(f"Clip does not fit the preview cycle: {suffix}")
        label(title, x, z)
    label("14-BONE SKILL / TURN REPERTOIRE", 0, 2.2, 0.2)
    label("HALF SPEED | in place | native clips + presentation reset", 0, 2.02, 0.09)
    report = validate(rigs)
    report.update({"fps": 100, "cycle_frames": CYCLE, "playback_speed": 0.5,
                   "source_sha256": hashes, "clips": entries,
                   "note": "Source library has no stepover/elastico/rabona; these are "
                           "all manual ball-carrying turn, spin and hold-ball clips. "
                           "No ball mesh is shown because hold-ball clips carry no "
                           "baked touch data."})
    scene.frame_set(20)
    player.select_only([rigs[0]])
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                space = area.spaces.active
                space.shading.type = "MATERIAL"
                space.region_3d.view_rotation = Quaternion((1, 0, 0), math.pi / 2)
                space.region_3d.view_perspective = "ORTHO"
                space.region_3d.view_location = (0, 0, -6.0)
                space.region_3d.view_distance = 16
    path = output / "skills_14_in_place.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(path))
    bpy.ops.wm.open_mainfile(filepath=str(path))
    loaded = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    report["saved_blend_reopen"] = validate(loaded)
    for p, original in hashes.items():
        if hashlib.sha256(Path(p).read_bytes()).hexdigest() != original:
            raise AssertionError("Modified source assets")
    player.write_json(output / "validation.json", report)
    print("SKILL_GALLERY_OK", path, report, flush=True)


if __name__ == "__main__":
    main()

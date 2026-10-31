"""A1a: convert the 10 legacy .anim motion templates to canonical 14-joint GLBs.

This is intentionally a source-import step, not the procedural generator. It
gives the later generator a versioned, skeleton-checked template input instead
of reading data/media/animations/templates at build time.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

import animation_assets as assets
import legacy
import simanim

REPO = Path(__file__).resolve().parents[2]


def _sample(tracks, name, frame, frame_count):
    keys = tracks.get(name)
    if not keys:
        return (0.0, 0.0, 0.0, 1.0) if name != "player" else (0.0, 0.0, 0.0)
    if frame >= frame_count:
        frame = frame_count - 1
    return legacy.sample_track(keys, frame, name != "player")


def _vec(values):
    return (float(values[0]), float(values[1]), 0.0)


def _velocity(a, b, seconds):
    return tuple((b[i] - a[i]) / seconds for i in range(2))


def build_clip(path, template_id):
    tracks, _ = legacy.load_animation(path)
    last = max(max(frame for frame, _ in keys) for keys in tracks.values())
    frame_count = last + 1
    if frame_count < 2:
        raise ValueError(f"Template is too short: {path}")
    poses = []
    roots = []
    for frame in range(frame_count):
        row = []
        for name in simanim.BODY_PARTS:
            values = _sample(tracks, name, frame, frame_count)
            if name == "player":
                row.append((0.0, 0.0, 0.0, 1.0, float(values[0]), float(values[1]), float(values[2])))
            else:
                row.append(tuple(float(x) for x in values) + (0.0, 0.0, 0.0))
        poses.append(row)
        roots.append((float(row[13][4]), float(row[13][5]), 0.0))

    first = _sample(tracks, "player", 0, frame_count)
    after_first = _sample(tracks, "player", 3, frame_count)
    before_last = _sample(tracks, "player", frame_count - 4, frame_count)
    last = _sample(tracks, "player", frame_count - 1, frame_count)
    incoming = _velocity(first, after_first, 0.03)
    outgoing = _velocity(before_last, last, 0.03)
    translation = (last[0] - first[0], last[1] - first[1], 0.0)
    metadata = {
        "action_type": 14,  # e_DefString_Movement
        "incoming_velocity": (incoming[0] ** 2 + incoming[1] ** 2) ** 0.5,
        "outgoing_velocity": (outgoing[0] ** 2 + outgoing[1] ** 2) ** 0.5,
        "incoming_body_angle": 0.0,
        "outgoing_body_angle": 0.0,
        "outgoing_angle": 0.0,
        "difficulty": 0.0,
        "touch_frame": -1,
        "quadrant": 0,
        "last_ditch": False,
        "base_animation": False,
        "idle_level": 0.0,
        "special_var1": 0.0,
        "special_var2": 0.0,
        "incoming_body_direction": (0.0, -1.0, 0.0),
        "outgoing_direction": (0.0, -1.0, 0.0),
        "incoming_ball_direction": (0.0, 0.0, 0.0),
        "outgoing_ball_direction": (0.0, 0.0, 0.0),
        "incoming_ball_direction_max_deviation": 0.0,
        "outgoing_ball_direction_max_deviation": 0.0,
        "trip_type": 0,
        "current_foot": 1,
        "incoming_retain_state": "",
        "outgoing_retain_state": "",
        "incoming_special_state": "",
        "forced_foot": "",
        "touch_foot": "",
        "outgoing_special_state": "",
        "touch_bodypart": "",
        "touch_max_power_factor": 0.0,
        "touch_difficulty_factor": 0.0,
        "priority": 1.0,
        "bump_direction": (0.0, 0.0, 0.0),
        "incoming_movement": (incoming[0], incoming[1], 0.0),
        "outgoing_movement": (outgoing[0], outgoing[1], 0.0),
        "translation": translation,
        "outgoing_foot": 0,
    }
    return simanim.Clip(template_id, path.stem, roots, poses, metadata, None, [])


def entry_for(clip, filename):
    return {
        "id": clip.id,
        "name": clip.name,
        "file": filename,
        "frame_count": len(clip.poses),
        "metadata": {key: (list(value) if isinstance(value, tuple) else value)
                     for key, value in clip.metadata.items()},
        "contact_position": clip.contact,
        "touches": clip.touches,
    }


def import_templates(source, output):
    source = Path(source)
    output = Path(output)
    skeleton = assets.read_skeleton(REPO / "assets/skeletons/player.json")
    files = sorted(source.glob("*.anim"))
    if len(files) != 10:
        raise ValueError(f"Expected 10 templates, found {len(files)}")
    output.mkdir(parents=True, exist_ok=True)
    manifest = {"schema": assets.SCHEMA, "rig_version": assets.RIG, "fps": 100,
                "skeleton": "../skeletons/player.json", "clips": []}
    for template_id, path in enumerate(files):
        clip = build_clip(path, template_id)
        filename = f"{template_id:02d}_{path.stem}.glb"
        assets.export_clip(output / filename, clip, skeleton)
        entry = entry_for(clip, filename)
        # Read the actual GLB back; the template import is fail-closed.
        roundtrip = assets.read_clip(output / filename, entry, skeleton)
        for expected, actual in zip(clip.poses, roundtrip.poses):
            for a, b in zip(expected, actual):
                if any(abs(x - y) > 2e-5 for x, y in zip(a, b)):
                    raise ValueError(f"Template roundtrip mismatch: {path.name}")
        manifest["clips"].append(entry)
    assets.json_write(output / "manifest.json", manifest)
    return len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=REPO / "data/media/animations/templates")
    parser.add_argument("--output", type=Path, default=REPO / "assets/motion_templates")
    args = parser.parse_args()
    count = import_templates(args.source, args.output)
    print(f"IMPORTED {count} motion templates -> {args.output}")


if __name__ == "__main__":
    main()

"""Strict stdlib reader for SIMANIM v7; layout: src/animation/clip.cpp."""
from dataclasses import dataclass
import math
from pathlib import Path
import struct

import legacy

BODY_PARTS = ("middle", "neck", "left_thigh", "right_thigh", "left_knee",
              "right_knee", "left_ankle", "right_ankle", "left_shoulder",
              "right_shoulder", "left_elbow", "right_elbow", "body", "player")


class Reader:
    def __init__(self, content):
        self.content, self.offset = content, 0

    def take(self, size):
        if size < 0 or size > len(self.content) - self.offset:
            raise ValueError(f"Truncated SIMANIM at byte {self.offset}")
        result = self.content[self.offset:self.offset + size]
        self.offset += size
        return result

    def values(self, fmt):
        values = struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt)))
        if any(isinstance(v, float) and not math.isfinite(v) for v in values):
            raise ValueError("Non-finite SIMANIM value")
        return values

    def u32(self):
        return self.values("I")[0]

    def string(self):
        return self.take(self.u32()).decode("utf-8")

    def flag(self):
        value = self.u32()
        if value not in (0, 1):
            raise ValueError("Invalid SIMANIM boolean")
        return bool(value)

    def metadata(self):
        result = {}
        for name, fmt in (
            ("action_type", "i"), ("incoming_velocity", "f"), ("outgoing_velocity", "f"),
            ("incoming_body_angle", "f"), ("outgoing_body_angle", "f"), ("outgoing_angle", "f"),
            ("difficulty", "f"), ("touch_frame", "i"), ("quadrant", "i")):
            result[name] = self.values(fmt)[0]
        for name in ("last_ditch", "base_animation"):
            result[name] = self.flag()
        for name in ("idle_level", "special_var1", "special_var2"):
            result[name] = self.values("f")[0]
        for name in ("incoming_body_direction", "outgoing_direction", "incoming_ball_direction",
                     "outgoing_ball_direction"):
            result[name] = self.values("3f")
        for name, fmt in (("incoming_ball_direction_max_deviation", "f"),
                          ("outgoing_ball_direction_max_deviation", "f"),
                          ("trip_type", "i"), ("current_foot", "i")):
            result[name] = self.values(fmt)[0]
        for name in ("incoming_retain_state", "outgoing_retain_state", "incoming_special_state",
                     "forced_foot", "touch_foot", "outgoing_special_state", "touch_bodypart"):
            result[name] = self.string()
        for name in ("touch_max_power_factor", "touch_difficulty_factor", "priority"):
            result[name] = self.values("f")[0]
        for name in ("bump_direction", "incoming_movement", "outgoing_movement", "translation"):
            result[name] = self.values("3f")
        result["outgoing_foot"] = self.values("i")[0]
        return result


@dataclass
class Clip:
    id: int
    name: str
    roots: list
    poses: list
    metadata: dict
    contact: tuple | None
    touches: list

    def world_pose(self, joints, frame):
        # GetKeyFrame holds local rotations, NOT world transforms. Non-root
        # position tracks are unused by Animation::Apply; use player.object offsets.
        local = dict(zip(BODY_PARTS, self.poses[frame]))
        result = {}
        for joint in joints:
            values = local[joint.name]
            rotation = legacy.normalize(values[:4])
            position = values[4:] if joint.name == "player" else joint.position
            if joint.name == "player":
                rotation = joint.rotation  # Apply sets root translation only.
            if joint.parent:
                parent_pos, parent_rot = result[joint.parent]
                position = tuple(a + b for a, b in zip(parent_pos, legacy.rotate(parent_rot, position)))
                rotation = legacy.multiply(parent_rot, rotation)
            result[joint.name] = position, rotation
        return result


def load(path: Path):
    if path.suffix == ".json":
        from animation_assets import load as load_catalog
        return load_catalog(path)
    reader = Reader(path.read_bytes())
    if reader.take(8) != b"SIMANIM1" or reader.u32() != 7:
        raise ValueError("Expected SIMANIM version 7")
    count = reader.u32()
    if not 0 < count <= 100000:
        raise ValueError("Invalid clip count")
    clips = []
    for index in range(count):
        name, frames = reader.string(), reader.u32()
        if not name or not 0 < frames <= 1000000:
            raise ValueError("Invalid clip name/frame count")
        # Check availability before allocating frame arrays.
        if frames * (12 + 14 * 28) > len(reader.content) - reader.offset:
            raise ValueError("Truncated SIMANIM frames")
        roots = [reader.values("3f") for _ in range(frames)]
        poses = [[reader.values("7f") for _ in BODY_PARTS] for _ in range(frames)]
        metadata = reader.metadata()
        contact = reader.values("3f") if reader.flag() else None
        touches = [{"frame": reader.values("i")[0], "position": reader.values("3f")}
                   for _ in range(reader.u32())]
        clips.append(Clip(index, name, roots, poses, metadata, contact, touches))
    if reader.offset != len(reader.content):
        raise ValueError("Unexpected trailing SIMANIM bytes")
    return clips

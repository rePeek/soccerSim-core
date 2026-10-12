"""Read-only import of the legacy player asset subset (stdlib only).

Binding semantics recovered from repository commit 361a764:
  src/gamedefines.cpp:GetVertexColors
  src/onthepitch/player/humanoid/humanoidbase.cpp:PrepareFullbodyModel
  src/scene/scene3d/node.cpp:Node::GetNodes
No runtime or simulation dependency. Quaternion tuples use x, y, z, w.
"""
from __future__ import annotations

from dataclasses import dataclass
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

IDENTITY = (0.0, 0.0, 0.0, 1.0)


def normalize(values):
    length = math.sqrt(sum(x * x for x in values))
    if length < 1e-12:
        raise ValueError("Cannot normalize a zero vector/quaternion")
    return tuple(x / length for x in values)


def multiply(a, b):
    x, y, z, w = a
    X, Y, Z, W = b
    return normalize((w * X + x * W + y * Z - z * Y,
                      w * Y - x * Z + y * W + z * X,
                      w * Z + x * Y - y * X + z * W,
                      w * W - x * X - y * Y - z * Z))


def inverse(q):
    return (-q[0], -q[1], -q[2], q[3])


def rotate(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    tx, ty, tz = (2 * (y * vz - z * vy), 2 * (z * vx - x * vz),
                  2 * (x * vy - y * vx))
    return (vx + w * tx + y * tz - z * ty,
            vy + w * ty + z * tx - x * tz,
            vz + w * tz + x * ty - y * tx)


def slerp(a, b, t):
    dot = sum(x * y for x, y in zip(a, b))
    if dot < 0:
        b, dot = tuple(-x for x in b), -dot
    if dot > 0.9995:
        return normalize(tuple(x * (1 - t) + y * t for x, y in zip(a, b)))
    angle = math.acos(max(-1.0, min(1.0, dot)))
    wa, wb = math.sin((1 - t) * angle), math.sin(t * angle)
    return normalize(tuple(x * wa + y * wb for x, y in zip(a, b)))


@dataclass(frozen=True)
class Joint:
    name: str
    parent: str | None
    position: tuple
    rotation: tuple


def load_rig(path: Path) -> list[Joint]:
    """Depth-first order is the legacy vertex-color joint ID order, excluding root."""
    joints = [Joint("player", None, (0.0, 0.0, 0.0), IDENTITY)]

    def visit(node, parent):
        name = node.findtext("name", "").strip()
        if not name or any(j.name == name for j in joints):
            raise ValueError(f"Missing/duplicate joint name: {name!r}")
        pos = tuple(float(x) for x in node.findtext("position", "0,0,0").split(","))
        angle, *axis = (float(x) for x in node.findtext("rotation", "0,0,0,0").split(","))
        if angle == 0:
            q = IDENTITY  # Legacy angle-axis 0,0,0,0 is identity, NOT a quaternion.
        else:
            axis = normalize(axis)
            half = math.radians(angle) / 2
            q = tuple(x * math.sin(half) for x in axis) + (math.cos(half),)
        joints.append(Joint(name, parent, pos, q))
        for child in node.findall("node"):
            visit(child, name)

    for node in ET.parse(path).getroot().findall("node"):
        visit(node, "player")
    return joints


def load_animation(path: Path):
    tracks = {}
    metadata = []
    in_metadata = False
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("<"):
            in_metadata = True
        if in_metadata or line.startswith("extension,"):
            metadata.append(line)
            continue
        name, *tokens = line.split(",")
        stride = 4 if name == "player" else 5
        if name in tracks or not tokens or len(tokens) % stride:
            raise ValueError(f"Invalid animation track {name!r} in {path}")
        keys = []
        for i in range(0, len(tokens), stride):
            frame = int(tokens[i])
            value = tuple(float(x) for x in tokens[i + 1:i + stride])
            if name != "player":
                value = normalize(value)
            if frame < 0 or (keys and frame <= keys[-1][0]):
                raise ValueError(f"Unsorted animation keys: {name}")
            keys.append((frame, value))
        tracks[name] = keys
    if "player" not in tracks:
        raise ValueError(f"Missing root motion in {path}")
    return tracks, metadata


def sample_track(keys, frame, quaternion=False):
    if frame <= keys[0][0]:
        return keys[0][1]
    for (start, a), (end, b) in zip(keys, keys[1:]):
        if frame <= end:
            t = (frame - start) / (end - start)
            return slerp(a, b, t) if quaternion else tuple(
                x * (1 - t) + y * t for x, y in zip(a, b))
    return keys[-1][1]


def evaluate_pose(joints, tracks, frame):
    """Raw, unsmoothed Animation::Apply: root translation, joint rotation replacement."""
    result = {}
    for joint in joints:
        position, rotation = joint.position, joint.rotation
        if joint.name in tracks:
            value = sample_track(tracks[joint.name], frame, joint.name != "player")
            if joint.name == "player":
                position = value
            else:
                rotation = value
        if joint.parent:
            parent_pos, parent_rot = result[joint.parent]
            position = tuple(a + b for a, b in zip(parent_pos, rotate(parent_rot, position)))
            rotation = multiply(parent_rot, rotation)
        result[joint.name] = (position, rotation)
    return result


def blocks(text, tag):
    """Extract brace-delimited ASE blocks, respecting quoted strings."""
    for match in re.finditer(r"\*" + re.escape(tag) + r"\b[^\n{]*\{", text):
        start, depth, quoted = match.end(), 1, False
        for i in range(start, len(text)):
            c = text[i]
            if c == '"':
                quoted = not quoted
            elif not quoted:
                depth += (c == "{") - (c == "}")
            if depth == 0:
                yield text[start:i]
                break
        else:
            raise ValueError(f"Unclosed ASE block: {tag}")


def indexed_vectors(text, tag):
    result = {}
    for match in re.finditer(r"\*" + tag + r"\s+(\d+)\s+([^\n]+)", text):
        index = int(match[1])
        if index in result:
            raise ValueError(f"Duplicate {tag} index {index}")
        result[index] = tuple(float(x) for x in match[2].split())
    return result


@dataclass
class Mesh:
    name: str
    material: int
    vertices: list
    faces: list
    uv_vertices: dict
    uv_faces: dict
    colors: dict
    color_faces: dict
    normals: list


def load_ase(path: Path):
    """ASE vertices are ALREADY world-space; NODE_TM is used for normals only."""
    text = path.read_text()
    materials = {}
    for block in blocks(text, "MATERIAL"):
        name = re.search(r'\*MATERIAL_NAME\s+"([^"]+)"', block)
        if name:
            materials[len(materials)] = name[1]
    meshes = []
    for block in blocks(text, "GEOMOBJECT"):
        name = re.search(r'\*NODE_NAME\s+"([^"]+)"', block)[1]
        verts = indexed_vectors(block, "MESH_VERTEX")
        if sorted(verts) != list(range(len(verts))):
            raise ValueError(f"Non-contiguous vertices: {name}")
        indexed_faces = {}
        for match in re.finditer(r"\*MESH_FACE\s+(\d+):\s+A:\s*(\d+)\s+B:\s*(\d+)\s+C:\s*(\d+)", block):
            indexed_faces[int(match[1])] = tuple(int(match[k]) for k in (2, 3, 4))
        if sorted(indexed_faces) != list(range(len(indexed_faces))):
            raise ValueError(f"Non-contiguous faces: {name}")
        faces = list(indexed_faces.values())
        rows = [normalize(tuple(float(x) for x in re.search(
            rf"\*TM_ROW{i}\s+([^\n]+)", block)[1].split())) for i in range(3)]
        normals = []
        for match in re.finditer(r"\*MESH_VERTEXNORMAL\s+\d+\s+([^\n]+)", block):
            n = tuple(float(x) for x in match[1].split())
            normals.append(normalize(tuple(sum(n[i] * rows[i][k] for i in range(3))
                                           for k in range(3))))
        if len(normals) != len(faces) * 3:
            raise ValueError(f"Missing per-corner normals: {name}")
        mesh = Mesh(name, int(re.search(r"\*MATERIAL_REF\s+(\d+)", block)[1]),
                    list(verts.values()), faces, indexed_vectors(block, "MESH_TVERT"),
                    indexed_vectors(block, "MESH_TFACE"), indexed_vectors(block, "MESH_VERTCOL"),
                    indexed_vectors(block, "MESH_CFACE"), normals)
        if len(mesh.color_faces) != len(faces) or len(mesh.uv_faces) != len(faces):
            raise ValueError(f"Missing color/UV faces: {name}")
        meshes.append(mesh)
    if not meshes:
        raise ValueError(f"No mesh found: {path}")
    return materials, meshes


def decode_weights(rgb, joint_count):
    """Each byte is joint_id * 10 + weight_digit (0..9), up to 3 influences."""
    decoded = []
    for component in rgb:
        if not 0 <= component <= 1:
            raise ValueError(f"Invalid encoded color: {rgb}")
        byte = math.floor(component * 255 + 0.5)  # C++ round, not Python banker rounding.
        joint, digit = divmod(byte, 10)
        if digit and joint >= joint_count:
            raise ValueError(f"Weight references nonexistent joint {joint}")
        decoded.append((joint, digit / 9))
    total = sum(weight for _, weight in decoded)
    if not total or not decoded[0][1]:
        raise ValueError(f"Missing primary weight: {rgb}")
    merged = {}
    for joint, weight in decoded:
        if weight > 0.01:
            merged[joint] = merged.get(joint, 0) + weight / total
    return tuple(merged.items())


def weight_map(meshes, joint_count):
    """Legacy GetVertexColors keys by global position, first occurrence wins."""
    result = {}
    conflicts = 0
    for mesh in meshes:
        for face_id, face in enumerate(mesh.faces):
            colors = mesh.color_faces[face_id]
            for vertex, color_id in zip(face, colors):
                position = mesh.vertices[vertex]
                weights = decode_weights(mesh.colors[int(color_id)], joint_count)
                if position in result and result[position] != weights:
                    conflicts += 1
                result.setdefault(position, weights)
    return result, conflicts


def unpose_vertex(position, weights, joint_names, base_pose, straight_pose):
    """Reproduce the legacy base-inverse -> straight preprocessing, including blending."""
    output = [0.0, 0.0, 0.0]
    for joint, weight in weights:
        name = joint_names[joint]
        base_pos, base_rot = base_pose[name]
        straight_pos, _ = straight_pose[name]
        local = rotate(inverse(base_rot), tuple(x - y for x, y in zip(position, base_pos)))
        for k in range(3):
            output[k] += (straight_pos[k] + local[k]) * weight
    return tuple(output)


def deform_vertex(position, weights, joint_names, rest_pose, pose):
    output = [0.0, 0.0, 0.0]
    for joint, weight in weights:
        name = joint_names[joint]
        rest_pos, _ = rest_pose[name]
        pos, rot = pose[name]
        local = rotate(rot, tuple(x - y for x, y in zip(position, rest_pos)))
        for k in range(3):
            output[k] += (pos[k] + local[k]) * weight
    return tuple(output)

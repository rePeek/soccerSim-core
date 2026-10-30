"""Canonical GLB animation sources and deterministic SIMANIM v7 cache compiler.

No Blender or third-party Python packages are needed. The supported GLB contract
is documented in assets/README.md; malformed/non-canonical files fail closed.
"""
from __future__ import annotations

import argparse
import io
import json
import math
from pathlib import Path
import re
import struct
import tempfile

import simanim

SCHEMA = "football_animation_catalog_v1"
RIG = "football_player_v1"
METADATA = (
    ("action_type", "i"), ("incoming_velocity", "f"), ("outgoing_velocity", "f"),
    ("incoming_body_angle", "f"), ("outgoing_body_angle", "f"), ("outgoing_angle", "f"),
    ("difficulty", "f"), ("touch_frame", "i"), ("quadrant", "i"),
    ("last_ditch", "b"), ("base_animation", "b"),
    ("idle_level", "f"), ("special_var1", "f"), ("special_var2", "f"),
    ("incoming_body_direction", "3f"), ("outgoing_direction", "3f"),
    ("incoming_ball_direction", "3f"), ("outgoing_ball_direction", "3f"),
    ("incoming_ball_direction_max_deviation", "f"), ("outgoing_ball_direction_max_deviation", "f"),
    ("trip_type", "i"), ("current_foot", "i"),
    ("incoming_retain_state", "s"), ("outgoing_retain_state", "s"),
    ("incoming_special_state", "s"), ("forced_foot", "s"), ("touch_foot", "s"),
    ("outgoing_special_state", "s"), ("touch_bodypart", "s"),
    ("touch_max_power_factor", "f"), ("touch_difficulty_factor", "f"), ("priority", "f"),
    ("bump_direction", "3f"), ("incoming_movement", "3f"), ("outgoing_movement", "3f"),
    ("translation", "3f"), ("outgoing_foot", "i"),
)


def json_read(path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(f"Duplicate JSON key: {key}")
            result[key] = value
        return result
    return json.loads(path.read_text(), object_pairs_hook=pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))


def json_write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n")


def yup(values):
    return (values[0], values[2], -values[1], *values[3:])


def zup(values):
    return (values[0], -values[2], values[1], *values[3:])


def finite(values):
    if any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in values):
        raise ValueError("Expected finite numeric values")
    return values


def read_glb(path):
    raw = path.read_bytes()
    if len(raw) < 20 or struct.unpack_from("<4sII", raw) != (b"glTF", 2, len(raw)):
        raise ValueError(f"Invalid GLB header: {path}")
    chunks, offset = {}, 12
    while offset < len(raw):
        if offset + 8 > len(raw):
            raise ValueError("Truncated GLB chunk")
        size, kind = struct.unpack_from("<II", raw, offset)
        offset += 8
        if size % 4 or offset + size > len(raw) or kind in chunks:
            raise ValueError("Invalid/duplicate GLB chunk")
        chunks[kind] = raw[offset:offset + size]
        offset += size
    if set(chunks) != {0x4E4F534A, 0x004E4942}:
        raise ValueError("Expected JSON and BIN chunks")
    doc = json.loads(chunks[0x4E4F534A])
    binary = chunks[0x004E4942]
    buffers = doc.get("buffers", [])
    if len(buffers) != 1 or "uri" in buffers[0] or not 0 <= len(binary) - buffers[0]["byteLength"] <= 3:
        raise ValueError("Expected one embedded binary buffer")
    return doc, binary


def write_glb(path, doc, binary):
    doc["buffers"] = [{"byteLength": len(binary)}]
    text = json.dumps(doc, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode()
    text += b" " * (-len(text) % 4)
    binary += b"\0" * (-len(binary) % 4)
    raw = (struct.pack("<4sII", b"glTF", 2, 12 + 16 + len(text) + len(binary))
           + struct.pack("<II", len(text), 0x4E4F534A) + text
           + struct.pack("<II", len(binary), 0x004E4942) + binary)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(raw)


class Builder:
    def __init__(self):
        self.binary = bytearray()
        self.doc = {"asset": {"version": "2.0", "generator": "football animation assets v1"},
                    "bufferViews": [], "accessors": []}

    def add(self, rows, kind, component=5126, bounds=False):
        width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4, "MAT4": 16}[kind]
        offset = len(self.binary)
        fmt = "f" if component == 5126 else "H"
        for row in rows:
            if len(row) != width:
                raise ValueError("Wrong accessor width")
            finite(row)
            self.binary.extend(struct.pack("<" + fmt * width, *row))
        size = len(self.binary) - offset
        self.binary.extend(b"\0" * (-len(self.binary) % 4))
        view = len(self.doc["bufferViews"])
        self.doc["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": size})
        index = len(self.doc["accessors"])
        spec = {"bufferView": view, "componentType": component, "count": len(rows), "type": kind}
        if bounds:
            spec.update(min=[min(row[i] for row in rows) for i in range(width)],
                        max=[max(row[i] for row in rows) for i in range(width)])
        self.doc["accessors"].append(spec)
        return index


def accessor(asset, index, kind):
    doc, binary = asset
    spec = doc["accessors"][index]
    if spec["componentType"] != 5126 or spec["type"] != kind or spec.get("sparse") or spec.get("normalized"):
        raise ValueError("Expected dense float32 animation accessor")
    width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[kind]
    view = doc["bufferViews"][spec["bufferView"]]
    if view["buffer"] != 0:
        raise ValueError("External buffer")
    start = view.get("byteOffset", 0) + spec.get("byteOffset", 0)
    stride = view.get("byteStride", 4 * width)
    count = spec["count"]
    end = start + max(0, count - 1) * stride + 4 * width
    if not 0 < count <= 1000000 or stride < 4 * width or stride % 4 or start % 4:
        raise ValueError("Invalid accessor layout")
    if start < view.get("byteOffset", 0) or end > view.get("byteOffset", 0) + view["byteLength"] or end > len(binary):
        raise ValueError("Animation accessor exceeds buffer bounds")
    return [tuple(finite(struct.unpack_from("<" + "f" * width, binary, start + i * stride))) for i in range(count)]


def read_skeleton(path):
    skeleton = json_read(path)
    bones = skeleton["bones"]
    if skeleton["rig_version"] != RIG or {b["name"] for b in bones} != set(simanim.BODY_PARTS) or len(bones) != 14:
        raise ValueError("Unsupported skeleton")
    seen = set()
    for bone in bones:
        if bone["parent"] is not None and bone["parent"] not in seen:
            raise ValueError("Skeleton must be parent-first and acyclic")
        if bone["parent"] is None and bone["name"] != "player":
            raise ValueError("player must be the only root")
        if len(bone["offset_zup"]) != 3:
            raise ValueError("Invalid skeleton offset")
        finite(bone["offset_zup"])
        seen.add(bone["name"])
    return skeleton


def skeleton_globals(skeleton):
    result = {}
    for bone in skeleton["bones"]:
        parent = result.get(bone["parent"], (0, 0, 0))
        result[bone["name"]] = tuple(a + b for a, b in zip(parent, bone["offset_zup"]))
    return result


def inverse_bind(position):
    x, y, z = yup(position)
    return (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., -x, -y, -z, 1.)


def export_clip(path, clip, skeleton):
    builder = Builder()
    doc = builder.doc
    mapping = {name: i for i, name in enumerate(simanim.BODY_PARTS)}
    bones = {b["name"]: b for b in skeleton["bones"]}
    nodes = [{"name": name, "translation": yup(bones[name]["offset_zup"])} for name in simanim.BODY_PARTS]
    for name, bone in bones.items():
        if bone["parent"]:
            nodes[mapping[bone["parent"]]].setdefault("children", []).append(mapping[name])
    globals_ = skeleton_globals(skeleton)
    ibm = builder.add([inverse_bind(globals_[name]) for name in simanim.BODY_PARTS], "MAT4")
    doc["skins"] = [{"joints": list(range(14)), "skeleton": mapping["player"], "inverseBindMatrices": ibm}]
    # A transparent 3-vertex skin witness keeps skeleton-only GLBs importable.
    positions = builder.add([(0., 0., 0.), (0.00001, 0., 0.), (0., 0.00001, 0.)], "VEC3", bounds=True)
    joints = builder.add([(13, 0, 0, 0)] * 3, "VEC4", 5123)
    weights = builder.add([(1., 0., 0., 0.)] * 3, "VEC4")
    doc["materials"] = [{"name": "__invisible_rig_binding__", "alphaMode": "BLEND",
                         "pbrMetallicRoughness": {"baseColorFactor": [0, 0, 0, 0], "metallicFactor": 0}}]
    doc["meshes"] = [{"name": "__rig_binding__", "primitives": [{"attributes": {
        "POSITION": positions, "JOINTS_0": joints, "WEIGHTS_0": weights}, "material": 0}]}]
    nodes.append({"name": "__rig_binding__", "mesh": 0, "skin": 0,
                  "extras": {"purpose": "Import witness only, not character appearance"}})
    nodes.append({"name": "PlayerSkeleton", "children": [mapping["player"], 14]})
    doc.update(nodes=nodes, scene=0, scenes=[{"nodes": [15]}])
    times = builder.add([(i / 100,) for i in range(len(clip.poses))], "SCALAR", bounds=True)
    animation = {"name": clip.name, "samplers": [], "channels": []}
    for index, name in enumerate(simanim.BODY_PARTS):
        rotations = [yup(pose[index][:4]) for pose in clip.poses]
        output = builder.add(rotations, "VEC4")
        sampler = len(animation["samplers"])
        animation["samplers"].append({"input": times, "output": output, "interpolation": "LINEAR"})
        animation["channels"].append({"sampler": sampler, "target": {"node": index, "path": "rotation"}})
    output = builder.add([yup(pose[13][4:]) for pose in clip.poses], "VEC3")
    animation["samplers"].append({"input": times, "output": output, "interpolation": "LINEAR"})
    animation["channels"].append({"sampler": 14, "target": {"node": 13, "path": "translation"}})
    doc["animations"] = [animation]
    doc["extras"] = {"rig_version": RIG, "fps": 100}
    write_glb(path, doc, bytes(builder.binary))


def read_clip(path, entry, skeleton):
    asset = read_glb(path)
    doc, _ = asset
    if len(doc.get("animations", [])) != 1 or doc.get("extras", {}).get("rig_version") != RIG:
        raise ValueError(f"Expected canonical {RIG} animation: {path}")
    frames = entry["frame_count"]
    if not isinstance(frames, int) or not 1 <= frames <= 1000000:
        raise ValueError("Invalid frame count")
    nodes = doc["nodes"]
    indices = {n.get("name"): i for i, n in enumerate(nodes)}
    if any(sum(n.get("name") == b["name"] for n in nodes) != 1 for b in skeleton["bones"]):
        raise ValueError("Duplicate/missing joint")
    parents = {}
    for index, node in enumerate(nodes):
        for child in node.get("children", []):
            if child in parents or not 0 <= child < len(nodes):
                raise ValueError("Invalid node hierarchy")
            parents[child] = index
    for bone in skeleton["bones"]:
        index = indices[bone["name"]]
        node = nodes[index]
        if "matrix" in node or node.get("rotation", [0, 0, 0, 1]) != [0, 0, 0, 1] or node.get("scale", [1, 1, 1]) != [1, 1, 1]:
            raise ValueError("Non-canonical joint rest basis; export logical joint axes, not Blender display axes")
        translation = finite(node.get("translation", [0, 0, 0]))
        if len(translation) != 3 or any(abs(a - b) > 1e-7 for a, b in zip(translation, yup(bone["offset_zup"]))):
            raise ValueError("Joint rest offsets differ from the shared skeleton")
        if bone["parent"] and parents.get(index) != indices[bone["parent"]]:
            raise ValueError("Joint parent differs from shared skeleton")
    root_parent = nodes[parents[indices["player"]]]
    if any(root_parent.get(key, default) != default for key, default in
           (("translation", [0, 0, 0]), ("rotation", [0, 0, 0, 1]), ("scale", [1, 1, 1]))) or "matrix" in root_parent:
        raise ValueError("Armature object transform must be identity")
    animation = doc["animations"][0]
    if animation["name"] != entry["name"]:
        raise ValueError("Animation name differs from manifest")
    tracks = {}
    for channel in animation["channels"]:
        target = channel["target"]
        name, prop = nodes[target["node"]]["name"], target["path"]
        if name not in simanim.BODY_PARTS or prop not in ("rotation", "translation"):
            raise ValueError("Unsupported animation target")
        key = (name, prop)
        if key in tracks or (prop == "translation" and name != "player"):
            raise ValueError("Duplicate channel or unsupported non-root translation")
        sampler = animation["samplers"][channel["sampler"]]
        if sampler.get("interpolation", "LINEAR") != "LINEAR":
            raise ValueError("Cache input requires LINEAR baked samples")
        times = accessor(asset, sampler["input"], "SCALAR")
        if len(times) != frames or any(abs(t[0] - i / 100) > 2e-6 for i, t in enumerate(times)):
            raise ValueError("Cache input must be baked at exactly 100 Hz, starting at zero")
        values = accessor(asset, sampler["output"], "VEC4" if prop == "rotation" else "VEC3")
        if len(values) != frames:
            raise ValueError("Animation key count mismatch")
        if prop == "rotation" and any(abs(sum(v * v for v in q) - 1) > 1e-4 for q in values):
            raise ValueError("Invalid rotation quaternion")
        tracks[key] = [zup(v) for v in values]
    expected = {(name, "rotation") for name in simanim.BODY_PARTS} | {("player", "translation")}
    if set(tracks) != expected:
        raise ValueError("Missing baked rotation/root translation channels")
    poses = [[tracks[(name, "rotation")][frame] + (tracks[(name, "translation")][frame]
              if name == "player" else (0., 0., 0.)) for name in simanim.BODY_PARTS] for frame in range(frames)]
    roots = [(p[13][4], p[13][5], 0.) for p in poses]
    if set(entry["metadata"]) != {name for name, _ in METADATA}:
        raise ValueError("Missing/unknown football metadata")
    contact = entry["contact_position"]
    touches = entry["touches"]
    if contact is not None and (len(contact) != 3 or not finite(contact)):
        raise ValueError("Invalid contact position")
    for touch in touches:
        if not isinstance(touch["frame"], int) or not 0 <= touch["frame"] < frames or len(touch["position"]) != 3:
            raise ValueError("Invalid touch frame/position")
        finite(touch["position"])
    metadata = {name: tuple(entry["metadata"][name]) if fmt == "3f" else entry["metadata"][name]
                for name, fmt in METADATA}
    contact = tuple(contact) if contact is not None else None
    touches = [{"frame": t["frame"], "position": tuple(t["position"])} for t in touches]
    return simanim.Clip(entry["id"], entry["name"], roots, poses, metadata, contact, touches)


def load(manifest):
    manifest = Path(manifest).resolve()
    doc = json_read(manifest)
    if doc["schema"] != SCHEMA or doc["fps"] != 100 or doc["rig_version"] != RIG:
        raise ValueError("Unsupported animation catalog")
    # The skeleton path is intentionally outside the animation directory.
    skeleton_path = (manifest.parent / doc["skeleton"]).resolve()
    skeleton = read_skeleton(skeleton_path)
    entries = doc["clips"]
    if not 0 < len(entries) <= 100000:
        raise ValueError("Invalid catalog size")
    used, clips = set(), []
    for index, entry in enumerate(entries):
        path = (manifest.parent / entry["file"]).resolve()
        if path in used or not path.is_relative_to(manifest.parent) or path.suffix != ".glb" or entry["id"] != index:
            raise ValueError("Invalid/duplicate clip path or non-contiguous IDs")
        used.add(path)
        try:
            clips.append(read_clip(path, entry, skeleton))
        except (KeyError, IndexError, TypeError, struct.error) as error:
            raise ValueError(f"Malformed animation: {path}: {error}") from error
    return clips


def serialize(clips):
    stream = io.BytesIO()
    def pack(fmt, *values):
        finite(values)
        stream.write(struct.pack("<" + fmt, *values))
    def string(value):
        if not isinstance(value, str):
            raise ValueError("Expected metadata string")
        raw = value.encode()
        pack("I", len(raw))
        stream.write(raw)
    stream.write(b"SIMANIM1")
    pack("II", 7, len(clips))
    for clip in clips:
        string(clip.name)
        pack("I", len(clip.poses))
        for root in clip.roots:
            pack("3f", *root)
        for pose in clip.poses:
            for row in pose:
                pack("7f", *row)
        for name, fmt in METADATA:
            value = clip.metadata[name]
            if fmt == "s":
                string(value)
            elif fmt == "b":
                if type(value) is not bool:
                    raise ValueError("Expected boolean metadata")
                pack("I", int(value))
            else:
                pack(fmt, *value) if fmt == "3f" else pack(fmt, value)
        pack("I", int(clip.contact is not None))
        if clip.contact is not None:
            pack("3f", *clip.contact)
        pack("I", len(clip.touches))
        for touch in clip.touches:
            pack("i3f", touch["frame"], *touch["position"])
    return stream.getvalue()


def bake(manifest, output):
    payload = serialize(load(manifest))
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Failures never replace a valid prior cache with a partial file.
    with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(payload)
            stream.close()
            temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)
    return payload


def migrate(source, root):
    """One-time import of the legacy cache; never part of the normal build."""
    import legacy
    clips = simanim.load(source)
    joints = legacy.load_rig(root / "data/media/objects/players/player.object")
    skeleton = {"rig_version": RIG, "units": "metres", "up_axis": "Y",
                "runtime_up_axis": "Z", "body_part_order": simanim.BODY_PARTS,
                "bones": [{"name": j.name, "parent": j.parent, "offset_zup": j.position} for j in joints]}
    json_write(root / "assets/skeletons/player.json", skeleton)
    directory = root / "assets/animations"
    entries = []
    for clip in clips:
        # All ignored legacy position tracks must actually be zero. Otherwise
        # migration must explicitly extend the contract, never silently drop data.
        if any(any(v != 0 for v in row[4:]) for pose in clip.poses for row in pose[:13]):
            raise ValueError("Non-root position data requires an explicit schema extension")
        name = clip.name.split("/animations/")[-1].replace(".anim", "")
        slug = re.sub(r"[^A-Za-z0-9_.-]+", "_", name).strip("._")[:110]
        filename = f"{clip.id:04d}_{slug}.glb"
        export_clip(directory / filename, clip, skeleton)
        entries.append({"id": clip.id, "name": clip.name, "file": filename,
                        "frame_count": len(clip.poses), "metadata": clip.metadata,
                        "contact_position": clip.contact, "touches": clip.touches})
    manifest = directory / "manifest.json"
    json_write(manifest, {"schema": SCHEMA, "rig_version": RIG, "fps": 100,
                         "skeleton": "../../skeletons/player.json", "clips": entries})
    if serialize(load(manifest)) != source.read_bytes():
        raise AssertionError("Migration lost data: rebuilt runtime cache is not byte-identical")
    print(f"MIGRATED {len(clips)} clips; GLB -> cache is byte-identical")


def canonical_character(source, output, skeleton_path):
    """Convert Blender display-bone bases to the canonical logical joint basis.

Mesh positions, topology, UVs, weights, textures and material data are untouched;
only rest joint transforms and inverse binds change together.
"""
    skeleton = read_skeleton(skeleton_path)
    doc, raw = read_glb(source)
    binary = bytearray(raw)
    if doc.get("animations") or len(doc["skins"]) != 1:
        raise ValueError("Expected one static skinned character")
    globals_ = skeleton_globals(skeleton)
    bones = {b["name"]: b for b in skeleton["bones"]}
    skin = doc["skins"][0]
    for node_index in skin["joints"]:
        node = doc["nodes"][node_index]
        if node["name"] not in bones:
            raise ValueError("Unexpected character joint")
        node["translation"] = yup(bones[node["name"]]["offset_zup"])
        for key in ("matrix", "rotation", "scale"):
            node.pop(key, None)
    spec = doc["accessors"][skin["inverseBindMatrices"]]
    view = doc["bufferViews"][spec["bufferView"]]
    if spec["type"] != "MAT4" or spec["componentType"] != 5126 or spec["count"] != 14:
        raise ValueError("Unsupported character inverse bind layout")
    for i, node_index in enumerate(skin["joints"]):
        matrix = inverse_bind(globals_[doc["nodes"][node_index]["name"]])
        offset = view.get("byteOffset", 0) + spec.get("byteOffset", 0) + i * view.get("byteStride", 64)
        struct.pack_into("<16f", binary, offset, *matrix)
    doc["extras"] = {"rig_version": RIG, "skeleton": "../../skeletons/player.json"}
    for node in doc["nodes"]:
        if "rig_version" in node.get("extras", {}):
            node["extras"]["rig_version"] = RIG
    write_glb(output, doc, bytes(binary[:doc["buffers"][0]["byteLength"]]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    compile_ = commands.add_parser("bake")
    compile_.add_argument("--manifest", type=Path, required=True)
    compile_.add_argument("--output", type=Path, required=True)
    migrate_ = commands.add_parser("migrate-legacy", help="One-time migration/provenance utility only")
    migrate_.add_argument("--source", type=Path, required=True)
    migrate_.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    character = commands.add_parser("character")
    character.add_argument("--source", type=Path, required=True)
    character.add_argument("--output", type=Path, required=True)
    character.add_argument("--skeleton", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "bake":
        payload = bake(args.manifest, args.output)
        print(f"BAKED {len(payload)} bytes -> {args.output}")
    elif args.command == "migrate-legacy":
        migrate(args.source, args.root)
    else:
        canonical_character(args.source, args.output, args.skeleton)


if __name__ == "__main__":
    main()

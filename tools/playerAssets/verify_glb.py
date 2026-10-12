"""Validate the exported BYTES, then reimport both GLBs in Blender.

Run: blender -b --factory-startup --python-exit-code 1 --python this_file --
No Godot/runtime dependency. Only the converter's uncompressed glTF subset is read.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import legacy

COMPONENTS = {5121: ("B", 1), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load_glb(path):
    raw = path.read_bytes()
    if struct.unpack_from("<4sII", raw) != (b"glTF", 2, len(raw)):
        raise AssertionError(f"Invalid GLB header: {path}")
    offset, document, binary = 12, None, None
    while offset < len(raw):
        size, kind = struct.unpack_from("<II", raw, offset)
        chunk = raw[offset + 8:offset + 8 + size]
        if len(chunk) != size or size % 4:
            raise AssertionError("Malformed GLB chunk")
        if kind == 0x4E4F534A:
            document = json.loads(chunk)
        elif kind == 0x004E4942:
            binary = chunk
        offset += size + 8
    if offset != len(raw) or document is None or binary is None:
        raise AssertionError("Incomplete GLB")
    return document, binary


def accessor(asset, index):
    document, binary = asset
    spec = document["accessors"][index]
    if spec.get("sparse") or spec.get("normalized"):
        raise AssertionError("Unexpected sparse/normalized accessor")
    view = document["bufferViews"][spec["bufferView"]]
    kind, size = COMPONENTS[spec["componentType"]]
    width = WIDTHS[spec["type"]]
    offset = view.get("byteOffset", 0) + spec.get("byteOffset", 0)
    stride = view.get("byteStride", size * width)
    return [struct.unpack_from("<" + kind * width, binary, offset + i * stride)
            for i in range(spec["count"])]


def matrix(values):
    return Matrix([values[i:i + 4] for i in range(0, 16, 4)]).transposed()


def quat(values):
    return Quaternion((values[3], values[0], values[1], values[2]))


def node_matrix(node):
    if "matrix" in node:
        return matrix(node["matrix"])
    return Matrix.LocRotScale(Vector(node.get("translation", (0, 0, 0))),
                              quat(node.get("rotation", (0, 0, 0, 1))),
                              Vector(node.get("scale", (1, 1, 1))))


def sample(keys, values, time, rotation=False, interpolation="LINEAR"):
    if time <= keys[0][0]:
        return values[0]
    for i in range(1, len(keys)):
        if time <= keys[i][0]:
            if interpolation == "STEP":
                return values[i] if time == keys[i][0] else values[i - 1]
            bias = (time - keys[i - 1][0]) / (keys[i][0] - keys[i - 1][0])
            if rotation:
                q = quat(values[i - 1]).slerp(quat(values[i]), bias)
                return (q.x, q.y, q.z, q.w)
            return tuple(a * (1 - bias) + b * bias for a, b in zip(values[i - 1], values[i]))
    return values[-1]


def world_matrices(document, overrides=None):
    overrides = overrides or {}
    worlds = {}
    def visit(index, parent):
        node = document["nodes"][index]
        node = dict(node, **overrides.get(node.get("name"), {}))
        worlds[index] = parent @ node_matrix(node)
        for child in node.get("children", []):
            visit(child, worlds[index])
    for root in document["scenes"][document.get("scene", 0)]["nodes"]:
        visit(root, Matrix.Identity(4))
    return worlds


def yup(position):
    return Vector((position[0], position[2], -position[1]))


def validate_bytes(output, data):
    body = load_glb(output / "default_body.glb")
    animation = load_glb(output / "animations/test_walk.glb")
    doc, binary = body
    anim_doc, _ = animation
    skeleton = json.loads((output / "skeleton.json").read_text())
    expected_names = {j["name"] for j in skeleton["bones"]}
    if doc.get("animations"):
        raise AssertionError("Body GLB must not contain animations")
    for document in (doc, anim_doc):
        if len(document.get("skins", [])) != 1:
            raise AssertionError("Expected one shared skeleton per package")
        names = {document["nodes"][i]["name"] for i in document["skins"][0]["joints"]}
        if names != expected_names:
            raise AssertionError("Export lost joints")
    if len(anim_doc.get("animations", [])) != 1 or len(anim_doc["meshes"]) != 1:
        raise AssertionError("Animation pack contains unexpected assets")
    witness_positions = accessor(animation, anim_doc["meshes"][0]["primitives"][0]["attributes"]["POSITION"])
    if len(witness_positions) != 3:
        raise AssertionError("Animation witness is not a 3-vertex triangle")
    manifest = json.loads((output / "animations/manifest.json").read_text())
    if anim_doc["animations"][0]["name"] != manifest["clips"][0]["name"]:
        raise AssertionError("Animation name differs from manifest")
    for image in doc["images"]:
        if image.get("mimeType") != "image/png" or "uri" in image:
            raise AssertionError("Textures must be embedded PNGs")
        view = doc["bufferViews"][image["bufferView"]]
        if binary[view.get("byteOffset", 0):][:8] != b"\x89PNG\r\n\x1a\n":
            raise AssertionError("Invalid embedded PNG")
    body_world = world_matrices(doc)
    anim_world = world_matrices(anim_doc)
    body_names = {n.get("name"): i for i, n in enumerate(doc["nodes"])}
    anim_names = {n.get("name"): i for i, n in enumerate(anim_doc["nodes"])}
    for name in expected_names:
        a, b = body_world[body_names[name]], anim_world[anim_names[name]]
        if max(abs(a[r][c] - b[r][c]) for r in range(4) for c in range(4)) > 1e-6:
            raise AssertionError(f"Body/animation rest transforms differ: {name}")
    player = data / "media/objects/players"
    joints = legacy.load_rig(player / "player.object")
    names = [j.name for j in joints[1:]]
    _, meshes = legacy.load_ase(player / "models/fullbody.ase")
    weights, _ = legacy.weight_map(meshes, len(names))
    base, _ = legacy.load_animation(data / "media/animations/base.anim.util")
    straight, _ = legacy.load_animation(data / "media/animations/straight.anim.util")
    walk, _ = legacy.load_animation(data / "media/animations/templates/walkD000.anim")
    base_pose = legacy.evaluate_pose(joints, base, 0)
    rest_pose = legacy.evaluate_pose(joints, straight, 0)
    source = {m.name: [(legacy.unpose_vertex(p, weights[p], names, base_pose, rest_pose), weights[p])
                       for p in m.vertices] for m in meshes}
    skin = doc["skins"][0]
    inverse_binds = [matrix(v) for v in accessor(body, skin["inverseBindMatrices"])]
    exported_vertices = []
    rest_error = 0.0
    for node_id, node in enumerate(doc["nodes"]):
        if "mesh" not in node:
            continue
        reference = source[node["extras"]["legacy_mesh"]]
        for primitive in doc["meshes"][node["mesh"]]["primitives"]:
            attributes = primitive["attributes"]
            positions = accessor(body, attributes["POSITION"])
            joint_ids = accessor(body, attributes["JOINTS_0"])
            influences = accessor(body, attributes["WEIGHTS_0"])
            for p, ids, w in zip(positions, joint_ids, influences):
                if abs(sum(w) - 1) > 1e-6:
                    raise AssertionError("Exported weights are not normalized")
                rest_point = body_world[node_id] @ Vector(p)
                original, original_weights = min(reference, key=lambda entry: (yup(entry[0]) - rest_point).length)
                error = (yup(original) - rest_point).length
                rest_error = max(rest_error, error)
                if error > 2e-5:
                    raise AssertionError("Exported geometry is not the straight rest mesh")
                exported_vertices.append((Vector(p), ids, w, original, original_weights))
    channels = []
    clip = anim_doc["animations"][0]
    for channel in clip["channels"]:
        target = channel["target"]
        sampler = clip["samplers"][channel["sampler"]]
        values = accessor(animation, sampler["output"])
        interpolation = sampler.get("interpolation", "LINEAR")
        # Blender optimizes constant translation/scale tracks to STEP.
        if interpolation not in {"STEP", "LINEAR"}:
            raise AssertionError("Expected baked LINEAR/STEP animation")
        channels.append((anim_doc["nodes"][target["node"]]["name"], target["path"],
                         accessor(animation, sampler["input"]), values, interpolation))
    last = max(keys[-1][0] for keys in walk.values())
    if abs(max(keys[-1][0] for _, _, keys, _, _ in channels) - last / 100) > 1e-6:
        raise AssertionError("Wrong GLB animation timebase")
    max_error = 0.0
    for frame in range(last + 1):
        overrides = {}
        for name, path, keys, values, interpolation in channels:
            overrides.setdefault(name, {})[path] = sample(
                keys, values, frame / 100, path == "rotation", interpolation)
        worlds = world_matrices(doc, overrides)
        matrices = [worlds[j] @ ibm for j, ibm in zip(skin["joints"], inverse_binds)]
        expected_pose = legacy.evaluate_pose(joints, walk, frame)
        for p, ids, w, original, original_weights in exported_vertices:
            actual = Vector((0, 0, 0))
            for joint_id, weight in zip(ids, w):
                actual += (matrices[joint_id] @ p) * weight
            expected = yup(legacy.deform_vertex(original, original_weights, names, rest_pose, expected_pose))
            max_error = max(max_error, (actual - expected).length)
    if max_error > 2e-5:
        raise AssertionError(f"Exported GLB skin+animation disagrees with legacy LBS: {max_error}")
    return {"glb_frames_checked": last + 1, "glb_vertices_checked_per_frame": len(exported_vertices),
            "glb_rest_vertex_error_m": rest_error, "glb_legacy_lbs_max_vertex_error_m": max_error,
            "embedded_png_count": len(doc["images"]), "animation_body_rest_compatible": True}


def validate_blender_import(output):
    result = {}
    for filename, expected_meshes, expected_animations in (
            ("default_body.glb", 12, 0), ("animations/test_walk.glb", 1, 1)):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.gltf(filepath=str((output / filename).resolve()),
                                  bone_heuristic="BLENDER", disable_bone_shape=True)
        rigs = [o for o in bpy.data.objects if o.type == "ARMATURE"]
        meshes = [o for o in bpy.data.objects if o.type == "MESH"]
        if len(rigs) != 1 or len(rigs[0].data.bones) != 14 or len(meshes) != expected_meshes:
            raise AssertionError(f"Blender did not recover the rig: {filename}")
        if len(bpy.data.actions) != expected_animations:
            raise AssertionError(f"Wrong imported animation count: {filename}")
        if any(not any(m.type == "ARMATURE" and m.object == rigs[0] for m in o.modifiers) for o in meshes):
            raise AssertionError("Imported mesh lost its armature modifier")
        result[filename] = {"bones": 14, "meshes": len(meshes), "actions": len(bpy.data.actions)}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    repo = Path(__file__).resolve().parents[2]
    parser.add_argument("--output", type=Path, default=repo / "build/player-assets")
    parser.add_argument("--data", type=Path, default=repo / "data")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    report = validate_bytes(args.output, args.data)
    report["blender_glb_reimport"] = validate_blender_import(args.output)
    path = args.output / "validation.json"
    existing = json.loads(path.read_text())
    existing.update(report)
    path.write_text(json.dumps(existing, indent=2) + "\n")
    print("PLAYER_GLB_VERIFIED", json.dumps(report))


if __name__ == "__main__":
    main()

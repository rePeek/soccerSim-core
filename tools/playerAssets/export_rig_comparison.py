"""Create static 14/20-bone GLBs and editable Blender comparison scenes.

Generate player_template.blend with export_player.py first. This is an offline
preview rig, NOT a migration of the runtime skeleton or animation library.
"""
import argparse
import hashlib
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_player as player
import verify_glb as glb

RIG_VERSION = "football_player_v2_20_preview"


def smoothstep(value):
    value = min(1.0, max(0.0, value))
    return value * value * (3 - 2 * value)


def add_bones(rig):
    """Keep EVERY old armature-space bind matrix, including shoulders."""
    original = {b.name: b.matrix_local.copy() for b in rig.data.bones}
    player.select_only([rig])
    bpy.ops.object.mode_set(mode="EDIT")
    bones = rig.data.edit_bones
    for side in ("left", "right"):
        shoulder = bones[f"{side}_shoulder"]
        clavicle = bones.new(f"{side}_clavicle")
        clavicle.head = (shoulder.head.x * 0.25, shoulder.head.y, shoulder.head.z)
        clavicle.tail = shoulder.head.copy()
        clavicle.parent = bones["middle"]
        shoulder.parent = clavicle
        shoulder.use_connect = False

        elbow = bones[f"{side}_elbow"]
        wrist = bones.new(f"{side}_wrist")
        wrist.head = elbow.head + Vector((0, 0, -0.24))
        wrist.tail = wrist.head + Vector((0, 0, -0.075))
        wrist.parent = elbow
        wrist.use_connect = False

        ankle = bones[f"{side}_ankle"]
        toe = bones.new(f"{side}_toe")
        toe.head = (ankle.head.x, -0.125, 0.045)
        toe.tail = (ankle.head.x, -0.20, 0.045)
        toe.parent = ankle
        toe.use_connect = False
    # Explicitly preserve edit-bone bases when inserting a rotated parent.
    for name, matrix in original.items():
        bones[name].matrix = matrix
    bpy.ops.object.mode_set(mode="OBJECT")
    for name, matrix in original.items():
        if max(abs(matrix[r][c] - rig.data.bones[name].matrix_local[r][c])
               for r in range(4) for c in range(4)) > 2e-5:
            raise AssertionError(f"Changed original bind transform: {name}")
    if len(rig.data.bones) != 20:
        raise AssertionError("Expected 20 bones")
    rig["rig_version"] = RIG_VERSION
    rig["preview_only"] = True
    return original


def transfer_weights(objects, rig):
    """Deterministic preview weights, not artist-authored production skinning."""
    report = {}
    for side in ("left", "right"):
        for old, new, influence in (
            (f"{side}_elbow", f"{side}_wrist",
             lambda p: smoothstep((1.08 - p.z) / 0.10)),
            (f"{side}_ankle", f"{side}_toe",
             lambda p: smoothstep((-p.y - 0.10) / 0.075)),
            (f"{side}_shoulder", f"{side}_clavicle",
             lambda p: 0.35 * smoothstep((p.z - 1.43) / 0.15)),
        ):
            affected, total = 0, 0.0
            for obj in objects:
                donor = obj.vertex_groups.get(old)
                if donor is None:
                    continue
                target = obj.vertex_groups.get(new) or obj.vertex_groups.new(name=new)
                for vertex in obj.data.vertices:
                    weight = next((g.weight for g in vertex.groups if g.group == donor.index), 0)
                    amount = weight * influence(vertex.co)
                    if amount <= 1e-8:
                        continue
                    remaining = weight - amount
                    if remaining > 1e-8:
                        donor.add([vertex.index], remaining, "REPLACE")
                    else:
                        donor.remove([vertex.index])
                    target.add([vertex.index], amount, "REPLACE")
                    affected += 1
                    total += amount
            if not affected:
                raise AssertionError(f"New bone has no skin influence: {new}")
            report[new] = {"weighted_vertices": affected, "total_weight": total}
    for obj in objects:
        obj["rig_version"] = RIG_VERSION
        for vertex in obj.data.vertices:
            total = sum(g.weight for g in vertex.groups if obj.vertex_groups[g.group].name in rig.data.bones)
            if abs(total - 1) > 2e-6:
                raise AssertionError(f"Unnormalized weights: {obj.name}/{vertex.index}: {total}")
    return report


def evaluated_vertices(objects):
    bpy.context.view_layer.update()
    graph = bpy.context.evaluated_depsgraph_get()
    result = {}
    for obj in objects:
        evaluated = obj.evaluated_get(graph)
        mesh = evaluated.to_mesh()
        try:
            result[obj.name] = [v.co.copy() for v in mesh.vertices]
        finally:
            evaluated.to_mesh_clear()
    return result


def check_new_bone_motion(rig, objects, reference):
    report = {}
    for bone in rig.pose.bones:
        if not bone.name.endswith(("_wrist", "_toe", "_clavicle")):
            continue
        bone.rotation_mode = "XYZ"
        bone.rotation_euler.x = 0.35
        posed = evaluated_vertices(objects)
        displacement = max((p - q).length for name in reference
                           for p, q in zip(reference[name], posed[name]))
        if displacement < 1e-4:
            raise AssertionError(f"Bone does not deform mesh: {bone.name}")
        report[bone.name] = {"test_rotation_radians": 0.35,
                             "max_vertex_displacement_m": displacement}
        bone.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    return report


def configure_view(rig):
    player.select_only([rig])
    rig.show_in_front = True
    rig.data.display_type = "OCTAHEDRAL"
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                area.spaces.active.shading.type = "MATERIAL"
                area.spaces.active.region_3d.view_distance = 3
                area.spaces.active.region_3d.view_location = (0, 0, 0.95)
    bpy.ops.object.mode_set(mode="POSE")
    for bone in rig.pose.bones:
        bone.select = bone.name.endswith(("_wrist", "_toe", "_clavicle"))


def save_scene(path, rig):
    configure_view(rig)
    bpy.ops.wm.save_as_mainfile(filepath=str(path))
    bpy.ops.object.mode_set(mode="OBJECT")


def save_demo(path, rig):
    scene = bpy.context.scene
    scene.render.fps = 30
    scene.frame_start, scene.frame_end = 0, 60
    for frame, amount in ((0, 0.0), (30, 1.0), (60, 0.0)):
        for bone in rig.pose.bones:
            if not bone.name.endswith(("_wrist", "_toe", "_clavicle")):
                continue
            bone.rotation_mode = "XYZ"
            if bone.name.endswith("_wrist"):
                bone.rotation_euler = (0.5 * amount, 0, 0)
            elif bone.name.endswith("_toe"):
                bone.rotation_euler = (0.4 * amount, 0, 0)
            else:
                sign = 1 if bone.name.startswith("left") else -1
                bone.rotation_euler = (0, 0, sign * 0.15 * amount)
            bone.keyframe_insert("rotation_euler", frame=frame, group=bone.name)
    rig.animation_data.action.name = "new_joints_demo_NOT_runtime_animation"
    scene.frame_set(0)
    save_scene(path, rig)


def verify_bytes(path, names):
    asset = glb.load_glb(path)
    doc, _ = asset
    if doc.get("animations") or len(doc.get("skins", [])) != 1:
        raise AssertionError("Expected static skinned GLB")
    skin = doc["skins"][0]
    if {doc["nodes"][i]["name"] for i in skin["joints"]} != set(names):
        raise AssertionError("Exported skeleton differs")
    worlds = glb.world_matrices(doc)
    inverse_binds = [glb.matrix(v) for v in glb.accessor(asset, skin["inverseBindMatrices"])]
    maximum = 0.0
    for node, inverse_bind in zip(skin["joints"], inverse_binds):
        delta = worlds[node] @ inverse_bind
        maximum = max(maximum, max(abs(delta[r][c] - (1 if r == c else 0))
                                  for r in range(4) for c in range(4)))
    if maximum > 2e-5:
        raise AssertionError(f"Broken rest skin matrices: {maximum}")
    for mesh in doc["meshes"]:
        for primitive in mesh["primitives"]:
            for weights in glb.accessor(asset, primitive["attributes"]["WEIGHTS_0"]):
                if abs(sum(weights) - 1) > 2e-6:
                    raise AssertionError("Export lost normalized weights")
    if not doc.get("images") or any("bufferView" not in i for i in doc["images"]):
        raise AssertionError("Expected embedded textures")
    return {"bones": len(names), "meshes": len(doc["meshes"]),
            "rest_skin_matrix_error": maximum, "embedded_images": len(doc["images"])}


def verify_reimport(path, count):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(path), bone_heuristic="BLENDER", disable_bone_shape=True)
    rigs = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    if len(rigs) != 1 or len(rigs[0].data.bones) != count or len(meshes) != 12 or bpy.data.actions:
        raise AssertionError(f"Unexpected Blender import: {path}")
    if any(not any(m.type == "ARMATURE" and m.object == rigs[0] for m in o.modifiers) for o in meshes):
        raise AssertionError("Reimport lost skin binding")
    result = {"bones": count, "meshes": len(meshes), "actions": 0}
    if count == 20:
        reference = evaluated_vertices(meshes)
        result["new_bone_deformation_tests"] = check_new_bone_motion(rigs[0], meshes, reference)
    return result


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", type=Path, default=repo / "build/player-assets/player_template.blend")
    parser.add_argument("--output", type=Path, default=repo / "build/player-rig-comparison")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    template, output = args.template.resolve(), args.output.resolve()
    for protected in (repo / "data", repo / "assets"):
        if output == protected or protected in output.parents:
            raise ValueError("Output must not overwrite source assets")
    if output == template or output in template.parents:
        raise ValueError("Use a separate output directory from the source template")
    if not template.is_file():
        raise FileNotFoundError("Generate player_template.blend with export_player.py first")
    template_hash = hashlib.sha256(template.read_bytes()).hexdigest()
    output.mkdir(parents=True, exist_ok=True)
    (output / "validation.json").unlink(missing_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(template))
    bpy.context.preferences.filepaths.save_version = 0
    rig = bpy.data.objects["PlayerSkeleton"]
    objects = [o for o in bpy.data.objects if o.type == "MESH"]
    if len(rig.data.bones) != 14 or rig.animation_data and rig.animation_data.action:
        raise ValueError("Expected the static 14-bone player template")
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    reference = evaluated_vertices(objects)
    names14 = [b.name for b in rig.data.bones]
    player.export_glb(output / "default_14_bones.glb", [rig] + objects)
    save_scene(output / "default_14_bones.blend", rig)

    add_bones(rig)
    weights = transfer_weights(objects, rig)
    rest = evaluated_vertices(objects)
    rest_error = max((a - b).length for name in reference for a, b in zip(reference[name], rest[name]))
    if rest_error > 2e-5:
        raise AssertionError(f"Rest mesh changed: {rest_error}")
    motion = check_new_bone_motion(rig, objects, rest)
    names20 = [b.name for b in rig.data.bones]
    player.export_glb(output / "default_20_bones.glb", [rig] + objects)
    save_scene(output / "default_20_bones.blend", rig)
    player.write_json(output / "skeleton_20.json", {
        "rig_version": RIG_VERSION, "preview_only": True,
        "bones": [{"name": b.name, "parent": b.parent.name if b.parent else None,
                   "head": list(b.head_local), "tail": list(b.tail_local),
                   "rest_matrix_blender": [list(row) for row in b.matrix_local]}
                  for b in rig.data.bones]})
    save_demo(output / "new_joints_demo_20.blend", rig)
    report = {"template_sha256": template_hash, "preview_only": True,
              "rest_vertex_error_m": rest_error, "weight_transfer": weights,
              "new_bone_deformation_tests": motion,
              "limitations": ["Procedural preview weights, not artist-authored production skinning.",
                              "Original low-poly topology; no extra hand/toe geometry.",
                              "No runtime migration, retargeting or Godot validation."]}
    for count, names in ((14, names14), (20, names20)):
        path = output / f"default_{count}_bones.glb"
        report[path.name] = verify_bytes(path, names)
        report[path.name]["blender_reimport"] = verify_reimport(path, count)
    if hashlib.sha256(template.read_bytes()).hexdigest() != template_hash:
        raise AssertionError("Modified original template")
    player.write_json(output / "validation.json", report)
    print("RIG_COMPARISON_OK", output, report, flush=True)


if __name__ == "__main__":
    main()

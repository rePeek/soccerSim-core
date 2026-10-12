"""Run with Blender --background --factory-startup --python this_file -- [options]."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import legacy

RIG_VERSION = "football_player_v1_preview"


def quaternion(q):
    return Quaternion((q[3], q[0], q[1], q[2]))


def transform(position, rotation):
    return Matrix.Translation(Vector(position)) @ quaternion(rotation).to_matrix().to_4x4()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def build_rig(joints, rest_pose):
    data = bpy.data.armatures.new("PlayerSkeleton")
    rig = bpy.data.objects.new("PlayerSkeleton", data)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    for joint in joints:
        bone = data.edit_bones.new(joint.name)
        bone.head = rest_pose[joint.name][0]
        children = [j for j in joints if j.parent == joint.name]
        if children:
            # A non-connected branching hierarchy; heads retain exact joint positions.
            bone.tail = rest_pose[children[0].name][0]
        else:
            offset = (0, -0.18, 0) if "ankle" in joint.name else (
                (0, 0, 0.18) if joint.name == "neck" else (0, 0, -0.25))
            bone.tail = bone.head + Vector(offset)
        if (bone.tail - bone.head).length < 1e-6:
            bone.tail = bone.head + Vector((0, 0, 0.1))
        if joint.parent:
            bone.parent = data.edit_bones[joint.parent]
        bone.use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")
    rig.show_in_front = True
    rig["rig_version"] = RIG_VERSION
    rig["legacy_up_axis"] = "Z"
    return rig


def load_texture(source, destination):
    if not source.is_file():
        raise FileNotFoundError(source)
    image = bpy.data.images.load(str(source.resolve()), check_existing=False)
    # Images load lazily; decode BEFORE changing the source filename.
    if not len(image.pixels):
        raise ValueError(f"Cannot decode image: {source}")
    image.filepath_raw = str(destination.resolve())
    image.file_format = "PNG"
    image.save()
    image.pack()
    return image


def make_material(name, image, source_name):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    shader = material.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Roughness"].default_value = 0.75
    shader.inputs["Metallic"].default_value = 0
    texture = material.node_tree.nodes.new("ShaderNodeTexImage")
    texture.name = "ReplaceableBaseColor"
    texture.label = "Replace this image for a texture mod"
    texture.image = image
    material.node_tree.links.new(texture.outputs["Color"], shader.inputs["Base Color"])
    material["legacy_material"] = source_name
    material["mod_slot"] = name
    return material


def build_mesh(mesh, rig, names, weights_by_position, base_pose, rest_pose, material):
    weights = [weights_by_position[p] for p in mesh.vertices]
    vertices = [legacy.unpose_vertex(p, w, names, base_pose, rest_pose)
                for p, w in zip(mesh.vertices, weights)]
    data = bpy.data.meshes.new(mesh.name)
    data.from_pydata(vertices, [], mesh.faces)
    data.update()
    uv_layer = data.uv_layers.new(name="UVMap")
    normals = []
    for face_id, polygon in enumerate(data.polygons):
        polygon.use_smooth = True
        for corner, loop_id in enumerate(polygon.loop_indices):
            uv = mesh.uv_vertices[int(mesh.uv_faces[face_id][corner])]
            # Blender loads images with bottom-left UV origin; the legacy SDL
            # upload used top-down pixels and -V. Do not apply that -V a second time.
            uv_layer.data[loop_id].uv = uv[:2]
            vertex_weights = weights[mesh.faces[face_id][corner]]
            normal = Vector((0, 0, 0))
            for joint, weight in vertex_weights:
                base_rot = base_pose[names[joint]][1]
                normal += Vector(legacy.rotate(legacy.inverse(base_rot),
                                               mesh.normals[face_id * 3 + corner])) * weight
            normals.append(normal.normalized())
    data.normals_split_custom_set(normals)
    # Keep mesh node names distinct from animation/bone node names (e.g. body).
    obj = bpy.data.objects.new(f"mesh_{mesh.name}", data)
    bpy.context.collection.objects.link(obj)
    obj.parent = rig
    modifier = obj.modifiers.new("PlayerSkin", "ARMATURE")
    modifier.object = rig
    groups = {name: obj.vertex_groups.new(name=name) for name in names}
    for vertex_id, vertex_weights in enumerate(weights):
        for joint, weight in vertex_weights:
            groups[names[joint]].add([vertex_id], weight, "REPLACE")
    data.materials.append(material)
    obj["legacy_mesh"] = mesh.name
    obj["rig_version"] = RIG_VERSION
    return obj, vertices, weights


def apply_pose(rig, joints, pose):
    desired = {}
    for joint in joints:
        # Blender's bone Y-axis follows its displayed tail. Preserve that basis
        # while the logical legacy skeleton has identity rest orientations.
        basis = rig.data.bones[joint.name].matrix_local.to_3x3().to_4x4()
        desired[joint.name] = transform(*pose[joint.name]) @ basis
        rest = rig.data.bones[joint.name].matrix_local
        target = desired[joint.name]
        if joint.parent:
            rest = rig.data.bones[joint.parent].matrix_local.inverted() @ rest
            target = desired[joint.parent].inverted() @ target
        bone = rig.pose.bones[joint.name]
        bone.rotation_mode = "QUATERNION"
        bone.matrix_basis = rest.inverted() @ target
    bpy.context.view_layer.update()


def make_action(rig, joints, tracks, last_frame):
    rig.animation_data_create()
    action = bpy.data.actions.new("test_walk_source")
    rig.animation_data.action = action
    action["source_fps"] = 100
    for frame in range(last_frame + 1):
        pose = legacy.evaluate_pose(joints, tracks, frame)
        apply_pose(rig, joints, pose)
        for joint in joints:
            bone = rig.pose.bones[joint.name]
            bone.keyframe_insert("rotation_quaternion", frame=frame, group=joint.name)
            if joint.name == "player":
                bone.keyframe_insert("location", frame=frame, group=joint.name)
    # Blender 4.4+ layered action API, rather than the removed Action.fcurves.
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for curve in bag.fcurves:
                    for point in curve.keyframe_points:
                        point.interpolation = "LINEAR"
    return action


def verify_deformation(rig, objects, joints, rest_pose, tracks, last_frame):
    names = [j.name for j in joints[1:]]
    maximum, joint_error = 0.0, 0.0
    # Check EVERY 10 ms source frame, against a stdlib implementation of the
    # recovered legacy LBS, not just against Blender's own exporter.
    for frame in range(last_frame + 1):
        bpy.context.scene.frame_set(frame)
        pose = legacy.evaluate_pose(joints, tracks, frame)
        depsgraph = bpy.context.evaluated_depsgraph_get()
        for joint in joints:
            actual = rig.pose.bones[joint.name].matrix.translation
            joint_error = max(joint_error, (actual - Vector(pose[joint.name][0])).length)
        for obj, vertices, weights in objects:
            evaluated = obj.evaluated_get(depsgraph)
            data = evaluated.to_mesh()
            try:
                if len(data.vertices) != len(vertices):
                    raise AssertionError("Unexpected topology change")
                for vertex, original, influences in zip(data.vertices, vertices, weights):
                    expected = legacy.deform_vertex(original, influences, names, rest_pose, pose)
                    maximum = max(maximum, (vertex.co - Vector(expected)).length)
            finally:
                evaluated.to_mesh_clear()
    if maximum > 2e-5 or joint_error > 2e-5:
        raise AssertionError(f"Legacy/Blender pose mismatch: vertices={maximum}, joints={joint_error}")
    return {"frames_checked": last_frame + 1,
            "legacy_lbs_max_vertex_error_m": maximum,
            "legacy_max_joint_position_error_m": joint_error}


def select_only(objects):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]


def export_glb(path, objects, animated=False):
    select_only(objects)
    bpy.ops.export_scene.gltf(
        filepath=str(path.resolve()), export_format="GLB", use_selection=True,
        export_animations=animated, export_animation_mode="ACTIVE_ACTIONS",
        export_nla_strips_merged_animation_name="test_walk_source",
        export_frame_range=True, export_force_sampling=True, export_frame_step=1,
        export_skins=True, export_def_bones=False, export_yup=True,
        export_normals=True, export_texcoords=True, export_extras=True,
        export_materials="EXPORT", export_image_format="AUTO")


def make_binding_witness(rig):
    """A transparent triangle makes animation-only GLB unambiguously a skinned rig.

    glTF has no standalone Skeleton object: without a skin, some importers treat
    the animated nodes as plain empties. This is NOT a body or mod appearance.
    """
    data = bpy.data.meshes.new("__rig_binding__")
    data.from_pydata([(0, 0, -0.001), (0.00001, 0, -0.001), (0, 0.00001, -0.001)], [], [(0, 1, 2)])
    obj = bpy.data.objects.new("__rig_binding__", data)
    bpy.context.collection.objects.link(obj)
    obj.parent = rig
    obj.modifiers.new("PlayerSkin", "ARMATURE").object = rig
    obj.vertex_groups.new(name="player").add([0, 1, 2], 1.0, "REPLACE")
    material = bpy.data.materials.new("__invisible_rig_binding__")
    material.use_nodes = True
    material.node_tree.nodes.get("Principled BSDF").inputs["Alpha"].default_value = 0
    material.surface_render_method = "DITHERED"
    data.materials.append(material)
    obj["purpose"] = "Skeleton import witness; discard when attaching animation library"
    return obj


def render_preview(output, frame, name):
    scene = bpy.context.scene
    scene.frame_set(frame)
    camera_data = bpy.data.cameras.new("PreviewCamera")
    camera = bpy.data.objects.new("PreviewCamera", camera_data)
    bpy.context.collection.objects.link(camera)
    camera.location = (2.4, -4, 1.9)
    # Follow the animated root, keeping the complete character in view.
    rig = bpy.data.objects.get("PlayerSkeleton")
    center = rig.pose.bones["player"].matrix.translation + Vector((0, 0, 0.9))
    camera.location += rig.pose.bones["player"].matrix.translation
    camera.rotation_euler = (center - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera_data.type, camera_data.ortho_scale = "ORTHO", 2.3
    scene.camera = camera
    light_data = bpy.data.lights.new("PreviewKey", "AREA")
    light_data.energy, light_data.shape, light_data.size = 450, "DISK", 4
    light = bpy.data.objects.new("PreviewKey", light_data)
    bpy.context.collection.objects.link(light)
    light.location = center + Vector((1, -3, 3))
    light.rotation_euler = (center - light.location).to_track_quat("-Z", "Y").to_euler()
    if scene.world is None:
        scene.world = bpy.data.worlds.new("PreviewWorld")
    scene.world.use_nodes = True
    scene.world.node_tree.nodes.get("Background").inputs["Color"].default_value = (0.3, 0.3, 0.3, 1)
    scene.world.node_tree.nodes.get("Background").inputs["Strength"].default_value = 0.6
    scene.render.engine = "CYCLES"
    scene.cycles.device, scene.cycles.samples = "CPU", 16
    scene.render.resolution_x = scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str((output / name).resolve())
    bpy.ops.render.render(write_still=True)
    for obj in (camera, light):
        bpy.data.objects.remove(obj, do_unlink=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    repo = Path(__file__).resolve().parents[2]
    parser.add_argument("--data", type=Path, default=repo / "data")
    parser.add_argument("--output", type=Path, default=repo / "build/player-assets")
    parser.add_argument("--render", action="store_true", help="Render CPU previews as PNG")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    data, output = args.data.resolve(), args.output.resolve()
    if output == data or data in output.parents:
        raise ValueError("Output must not be inside the immutable data directory")
    output.mkdir(parents=True, exist_ok=True)
    (output / "textures").mkdir(exist_ok=True)
    (output / "animations").mkdir(exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.preferences.filepaths.save_version = 0
    player = data / "media/objects/players"
    animation_dir = data / "media/animations"
    source_paths = [player / "player.object", player / "models/fullbody.ase",
                    animation_dir / "base.anim.util", animation_dir / "straight.anim.util",
                    animation_dir / "templates/walkD000.anim"]
    source_hashes = {str(p.relative_to(data)): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in source_paths}
    joints = legacy.load_rig(source_paths[0])
    names = [j.name for j in joints[1:]]
    materials, meshes = legacy.load_ase(source_paths[1])
    weights, conflicts = legacy.weight_map(meshes, len(names))
    base_tracks, _ = legacy.load_animation(source_paths[2])
    straight_tracks, _ = legacy.load_animation(source_paths[3])
    walk_tracks, metadata = legacy.load_animation(source_paths[4])
    for tracks in (base_tracks, straight_tracks, walk_tracks):
        if set(tracks) != {j.name for j in joints}:
            raise ValueError("Animation/rig joint names do not match")
    base_pose = legacy.evaluate_pose(joints, base_tracks, 0)
    rest_pose = legacy.evaluate_pose(joints, straight_tracks, 0)
    if any(abs(q[3]) < 1 - 1e-6 for _, q in rest_pose.values()):
        raise ValueError("Expected identity straight rest orientations")
    rig = build_rig(joints, rest_pose)
    texture_dir = player / "textures"
    team_dir = data / "databases/default/images_teams/primeradivision"
    texture_sources = {
        "kit": team_dir / "fcbarcelona_kit_02.bmp",
        "alternative_kit": team_dir / "realmadrid_kit_02.bmp",
        "skin": texture_dir / "skin01.bmp",
        "shoe": texture_dir / "shoe.bmp",
        "shoe_sole": texture_dir / "shoe_sole.bmp",
    }
    images = {name: load_texture(source, output / "textures" / f"{name}.png")
              for name, source in texture_sources.items()}
    slots = {"kit": "kit_shared", "skin": "face_skin", "knee": "body_skin_knee",
             "arm": "body_skin_arm", "shoe": "shoes", "shoe_sole": "shoe_soles"}
    material_objects = {}
    for material_id in sorted({mesh.material for mesh in meshes}):
        source_name = materials[material_id]
        texture_name = "skin" if source_name in {"skin", "knee", "arm"} else source_name
        if source_name not in slots:
            raise ValueError(f"Unmapped material: {source_name}")
        material_objects[material_id] = make_material(slots[source_name], images[texture_name], source_name)
    objects = [build_mesh(mesh, rig, names, weights, base_pose, rest_pose,
                          material_objects[mesh.material]) for mesh in meshes]
    body_objects = [entry[0] for entry in objects]
    scene = bpy.context.scene
    scene.render.fps = 100
    scene.frame_start = 0
    scene.frame_end = max(keys[-1][0] for keys in walk_tracks.values())
    scene.unit_settings.system = "METRIC"
    select_only([rig] + body_objects)
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                area.spaces.active.shading.type = "MATERIAL"
                area.spaces.active.region_3d.view_distance = 3
                area.spaces.active.region_3d.view_location = (0, 0, 0.9)
    # The actual mod template contains NO animation.
    bpy.ops.wm.save_as_mainfile(filepath=str(output / "player_template.blend"))
    export_glb(output / "default_body.glb", [rig] + body_objects)
    if args.render:
        render_preview(output, 0, "preview_rest.png")
    action = make_action(rig, joints, walk_tracks, scene.frame_end)
    validation = verify_deformation(rig, objects, joints, rest_pose, walk_tracks, scene.frame_end)
    scene.frame_set(0)
    select_only([rig] + body_objects)
    bpy.ops.wm.save_as_mainfile(filepath=str(output / "walk_preview.blend"))
    if args.render:
        render_preview(output, scene.frame_end // 2, "preview_walk.png")
        kit_material = next(m for m in material_objects.values() if m["mod_slot"] == "kit_shared")
        image_node = kit_material.node_tree.nodes["ReplaceableBaseColor"]
        original_image = image_node.image
        try:
            image_node.image = images["alternative_kit"]
            render_preview(output, scene.frame_end // 2, "preview_alternative_kit.png")
        finally:
            image_node.image = original_image
    witness = make_binding_witness(rig)
    export_glb(output / "animations/test_walk.glb", [rig, witness], animated=True)
    bpy.data.objects.remove(witness, do_unlink=True)
    write_json(output / "skeleton.json", {
        "rig_version": RIG_VERSION, "units": "metres", "blender_up_axis": "Z",
        "gltf_up_axis": "Y", "root_motion_bone": "player",
        "legacy_weight_joint_order": names,
        "bones": [{"name": j.name, "parent": j.parent, "local_offset": j.position,
                   "rest_matrix_blender": [list(row) for row in rig.data.bones[j.name].matrix_local]}
                  for j in joints], "source_sha256": source_hashes})
    write_json(output / "skin.json", {
        "rig_version": RIG_VERSION, "model": "default_body.glb",
        "material_slots": {m.name: {"legacy_material": m["legacy_material"],
                           "texture": "textures/" + Path(m.node_tree.nodes["ReplaceableBaseColor"].image.filepath).name}
                           for m in material_objects.values()},
        "alternative_kit": "textures/alternative_kit.png",
        "parts": [{"name": f"mesh_{m.name}", "source_name": m.name,
                   "material": material_objects[m.material].name} for m in meshes],
        "limitations": ["Shirt, shorts and socks currently share kit_shared and the original atlas.",
                        "No independent shirt/shorts mesh segmentation yet.",
                        "Hair, normal maps, physique/height changes and Godot assembly are not implemented."]})
    write_json(output / "animations/manifest.json", {
        "rig_version": RIG_VERSION, "clips": [{"name": action.name, "file": "test_walk.glb",
        "source": "media/animations/templates/walkD000.anim", "fps": 100,
        "duration_seconds": scene.frame_end / 100, "root_motion": "preserved",
        "loop": False, "source_metadata": metadata,
        "note": "Raw source template/half-step, not the baker's generated runtime locomotion clip."}],
        "binding_witness": "__rig_binding__: transparent 3-vertex mesh, not character appearance"})
    validation.update({"rig_version": RIG_VERSION, "joint_count": len(joints),
        "deform_joint_count": len(names), "mesh_count": len(meshes),
        "source_vertex_count": sum(len(m.vertices) for m in meshes),
        "triangle_count": sum(len(m.faces) for m in meshes),
        "max_influences": max(len(w) for w in weights.values()),
        "global_color_conflicts_first_wins": conflicts,
        "godot_tested": False, "source_sha256": source_hashes})
    validation["kit_texture_swap_rendered"] = args.render
    for p in source_paths:
        if hashlib.sha256(p.read_bytes()).hexdigest() != source_hashes[str(p.relative_to(data))]:
            raise AssertionError(f"Source was modified: {p}")
    write_json(output / "validation.json", validation)
    print("PLAYER_ASSETS_OK", json.dumps(validation))


if __name__ == "__main__":
    main()

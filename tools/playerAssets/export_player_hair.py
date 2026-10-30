"""Attach repository hairstyle meshes to the existing neck joint, without new bones.

Legacy placement: HumanoidBase::UpdateFullbodyNodes in commit 361a764 attaches
hair to joints[2] (neck). ASE vertices are already in that joint's local frame.
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
import export_rig_comparison as rig_tools
import export_run_comparison as comparison
import export_run_20 as run
import legacy
import simanim
import verify_glb as glb

STYLES = ("short01", "short02", "medium01", "medium02", "long01", "long02")
COLORS = ("black", "brown", "blonde", "darkblonde", "red")


def attach_hair(rig, path, material, levels):
    _, meshes = legacy.load_ase(path)
    if len(meshes) != 1:
        raise ValueError("Expected one hairstyle mesh")
    source = meshes[0]
    # Logical neck rest axes are identity; Blender's display-bone axes are not.
    # Use only its rest translation here, NOT its display-bone orientation.
    neck_position = rig.data.bones["neck"].matrix_local.translation
    data = bpy.data.meshes.new("hair_" + path.stem)
    data.from_pydata([Vector(p) + neck_position for p in source.vertices], [], source.faces)
    data.update()
    uv = data.uv_layers.new(name="UVMap")
    for index, polygon in enumerate(data.polygons):
        polygon.use_smooth = True
        for corner, loop in enumerate(polygon.loop_indices):
            uv.data[loop].uv = source.uv_vertices[int(source.uv_faces[index][corner])][:2]
    obj = bpy.data.objects.new("mesh_hair_" + path.stem, data)
    bpy.context.collection.objects.link(obj)
    obj.parent = rig
    data.materials.append(material)
    obj.vertex_groups.new(name="neck").add(list(range(len(data.vertices))), 1, "REPLACE")
    if levels:
        sub = obj.modifiers.new("HairSurfaceSubdivision", "SUBSURF")
        sub.levels = sub.render_levels = levels
        sub.uv_smooth = "PRESERVE_BOUNDARIES"
        player.select_only([obj])
        bpy.ops.object.modifier_apply(modifier=sub.name)
    obj.modifiers.new("HairSkin", "ARMATURE").object = rig
    obj["hairstyle"], obj["attachment_bone"] = path.stem, "neck"
    obj["rig_version"] = rig["rig_version"]
    obj["hair_physics"] = False
    data = obj.data
    data.calc_loop_triangles()
    if any(len(v.groups) != 1 or abs(v.groups[0].weight - 1) > 1e-6 for v in data.vertices):
        raise AssertionError("Hair must be rigidly skinned to neck")
    return obj, {"source_triangles": len(source.faces), "triangles": len(data.loop_triangles),
                 "vertices": len(data.vertices), "attachment_bone": "neck"}


def configure_view(center=(0, 0, 1.1), distance=3):
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                space = area.spaces.active
                space.shading.type = "MATERIAL"
                space.region_3d.view_rotation = Quaternion((1, 0, 0), math.pi / 2)
                space.region_3d.view_perspective = "ORTHO"
                space.region_3d.view_location = center
                space.region_3d.view_distance = distance


def verify_hair_motion(rig, hair, frames):
    rest = [v.co.copy() for v in hair.data.vertices]
    inverse_bind = rig.data.bones["neck"].matrix_local.inverted()
    maximum = 0.0
    for frame in range(frames):
        bpy.context.scene.frame_set(frame)
        evaluated = hair.evaluated_get(bpy.context.evaluated_depsgraph_get())
        mesh = evaluated.to_mesh()
        try:
            skin = rig.pose.bones["neck"].matrix @ inverse_bind
            if len(mesh.vertices) != len(rest):
                raise AssertionError("Hair topology changed")
            for vertex, point in zip(mesh.vertices, rest):
                maximum = max(maximum, (vertex.co - skin @ point).length)
        finally:
            evaluated.to_mesh_clear()
    if maximum > 2e-5:
        raise AssertionError(f"Hair does not follow head: {maximum}")
    return {"frames_checked": frames, "max_neck_skin_error_m": maximum}


def make_gallery(template, hair_dir, output, material, levels):
    # Keep the existing material (and packed image) through the scene reset.
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    scene = bpy.context.scene
    for index, style in enumerate(STYLES):
        x, z = (index % 3 - 1) * 0.6, 0.7 if index < 3 else 0
        rig, objects = comparison.append_character(template, 14, x)
        rig.name = "HairPreview_" + style
        rig.location.z = z
        # Head-only close-ups; preserve original coordinates/bind pose.
        for obj in objects:
            if obj.type == "MESH" and obj.get("legacy_mesh") != "head":
                bpy.data.objects.remove(obj, do_unlink=True)
        attach_hair(rig, hair_dir / (style + ".ase"), material, levels)
        text = bpy.data.curves.new("HairLabel", "FONT")
        text.body, text.size, text.align_x = style, 0.07, "CENTER"
        label = bpy.data.objects.new("Label_" + style, text)
        scene.collection.objects.link(label)
        label.location = (x, -0.3, z + 1.52)
        label.rotation_euler = (math.pi / 2, 0, 0)
    scene.frame_set(0)
    configure_view(center=(0, 0, 2.08), distance=2.5)
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.wm.save_as_mainfile(filepath=str(output / "hairstyles_preview.blend"))


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", type=Path, default=repo / "build/player-smooth/default_14_smooth.blend")
    parser.add_argument("--output", type=Path, default=repo / "build/player-hair")
    parser.add_argument("--style", choices=STYLES, default="short01")
    parser.add_argument("--color", choices=COLORS, default="black")
    parser.add_argument("--hair-levels", type=int, choices=(0, 1, 2), default=2)
    parser.add_argument("--gallery", action="store_true", help="Also save head close-ups of all six hairstyles")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    output, template = args.output.resolve(), args.template.resolve()
    for protected in (repo / "data", repo / "assets", template.parent):
        if output == protected or protected in output.parents or output in protected.parents:
            raise ValueError("Use an independent output directory")
    asset_dir = repo / "data/media/objects/players"
    hair_dir = asset_dir / "hairstyles"
    texture = asset_dir / "textures/hair" / (args.color + ".bmp")
    source = repo / "assets/animations/manifest.json"
    inputs = [template, source, texture] + [hair_dir / (style + ".ase") for style in STYLES]
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    output.mkdir(parents=True, exist_ok=True)
    (output / "textures").mkdir(exist_ok=True)
    (output / "validation.json").unlink(missing_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(template))
    bpy.context.preferences.filepaths.save_version = 0
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    rigs = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    if len(rigs) != 1 or len(rigs[0].data.bones) != 14:
        raise ValueError("Expected one 14-bone player template")
    rig = rigs[0]
    if rig.animation_data and rig.animation_data.action:
        raise ValueError("Expected a static template")
    if any(o.get("attachment_bone") for o in bpy.data.objects):
        raise ValueError("Template already contains hair")
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    bind = {b.name: b.matrix_local.copy() for b in rig.data.bones}
    image = player.load_texture(texture, output / "textures" / ("hair_" + args.color + ".png"))
    material = player.make_material("hair_" + args.color, image, "legacy_hair")
    hair, report = attach_hair(rig, hair_dir / (args.style + ".ase"), material, args.hair_levels)
    report.update({"style": args.style, "color": args.color, "source_sha256": hashes,
                   "hair_physics": False, "note": "Rigid head attachment, no extra bones or hair simulation"})
    objects = [o for o in bpy.data.objects if o.type == "MESH"]
    player.select_only([rig])
    rig.show_in_front = False
    configure_view()
    static_path = output / "default_14_hair.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(static_path))
    glb_path = output / "default_14_hair.glb"
    player.export_glb(glb_path, [rig] + objects)
    report["glb"] = rig_tools.verify_bytes(glb_path, bind.keys())
    asset = glb.load_glb(glb_path)
    doc, _ = asset
    hair_nodes = [n for n in doc["nodes"] if n.get("extras", {}).get("attachment_bone") == "neck"]
    if len(hair_nodes) != 1 or "skin" not in hair_nodes[0]:
        raise AssertionError("GLB lost skinned hair mesh")
    hair_mesh = doc["meshes"][hair_nodes[0]["mesh"]]
    skin = doc["skins"][hair_nodes[0]["skin"]]
    neck_index = next(i for i, node in enumerate(skin["joints"]) if doc["nodes"][node]["name"] == "neck")
    for primitive in hair_mesh["primitives"]:
        attributes = primitive["attributes"]
        for indices, weights in zip(glb.accessor(asset, attributes["JOINTS_0"]),
                                    glb.accessor(asset, attributes["WEIGHTS_0"])):
            if any(w > 1e-6 and i != neck_index for i, w in zip(indices, weights)):
                raise AssertionError("GLB hair does not reference the neck joint")
    clips = simanim.load(source)
    pair = []
    for suffix in ("/movement/sprint/000_idlelevel1.anim", "/movement/sprint/000_idlelevel1.anim_mirror"):
        found = [c for c in clips if c.name.endswith(suffix)]
        if len(found) != 1:
            raise ValueError("Expected sprint pair")
        pair.append(found[0])
    joints = legacy.load_rig(asset_dir / "player.object")
    run.make_action(rig, joints, pair, False, 5)
    scene = bpy.context.scene
    scene.render.fps, scene.render.fps_base = 100, 1
    scene.frame_start, scene.frame_end = 0, 240
    report["running_hair_skin"] = verify_hair_motion(rig, hair, 241)
    scene.frame_set(0)
    bpy.ops.wm.save_as_mainfile(filepath=str(output / "run_14_hair.blend"))
    # Check the exported file imports as a 14-bone rig with one extra mesh.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(glb_path), bone_heuristic="BLENDER", disable_bone_shape=True)
    imported_rigs = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    imported_meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    if len(imported_rigs) != 1 or len(imported_rigs[0].data.bones) != 14 or len(imported_meshes) != len(objects):
        raise AssertionError("GLB reimport lost hair or skeleton")
    report["blender_reimport"] = {"bones": 14, "meshes": len(imported_meshes)}
    if args.gallery:
        # Recreate the material after resetting Blender, with a packed PNG.
        image = bpy.data.images.load(str(output / "textures" / ("hair_" + args.color + ".png")))
        image.pack()
        material = player.make_material("hair_" + args.color, image, "legacy_hair")
        make_gallery(template, hair_dir, output, material, args.hair_levels)
    for path, original in hashes.items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != original:
            raise AssertionError("Modified input assets")
    player.write_json(output / "validation.json", report)
    print("PLAYER_HAIR_OK", output, report, flush=True)


if __name__ == "__main__":
    main()

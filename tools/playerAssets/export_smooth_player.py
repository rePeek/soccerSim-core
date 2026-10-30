"""Subdivision-smoothed 14-bone player preview, not a newly sculpted character."""
import argparse
import hashlib
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Quaternion

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_player as player
import export_rig_comparison as rig_tools
import export_run_comparison as comparison
import export_run_20 as run
import legacy
import simanim
import verify_glb as glb


def triangle_count(objects):
    total = 0
    for obj in objects:
        obj.data.calc_loop_triangles()
        total += len(obj.data.loop_triangles)
    return total


def smooth_mesh(obj, levels):
    mesh = obj.data
    # Keep open part boundaries fixed, so independently skinned pieces do not
    # acquire new gaps simply because the surface is subdivided.
    counts = {edge.key: 0 for edge in mesh.edges}
    for polygon in mesh.polygons:
        for key in polygon.edge_keys:
            counts[tuple(sorted(key))] += 1
    crease = mesh.attributes.get("crease_edge") or mesh.attributes.new("crease_edge", "FLOAT", "EDGE")
    boundary_count = 0
    for edge in mesh.edges:
        if counts[edge.key] == 1:
            crease.data[edge.index].value = 1.0
            boundary_count += 1
    for polygon in mesh.polygons:
        polygon.use_smooth = True
    modifier = obj.modifiers.new("PreviewSubdivision", "SUBSURF")
    modifier.subdivision_type = "CATMULL_CLARK"
    modifier.levels = modifier.render_levels = levels
    modifier.uv_smooth = "PRESERVE_BOUNDARIES"
    if hasattr(modifier, "use_custom_normals"):
        modifier.use_custom_normals = False
    player.select_only([obj])
    bpy.ops.object.modifier_move_to_index(modifier=modifier.name, index=0)
    # Apply only subdivision, retaining the real armature modifier and UVs.
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    # glTF's portable four-influence subset. Explicitly normalize after the
    # subdivision interpolation instead of relying on silent exporter pruning.
    pruned = 0
    for vertex in obj.data.vertices:
        weights = sorted([(g.group, g.weight) for g in vertex.groups if g.weight > 1e-8],
                         key=lambda item: item[1], reverse=True)
        if not weights:
            raise AssertionError(f"Unweighted vertex: {obj.name}/{vertex.index}")
        pruned += int(len(weights) > 4)
        keep = weights[:4]
        total = sum(w for _, w in keep)
        for index in [g.group for g in vertex.groups]:
            obj.vertex_groups[index].remove([vertex.index])
        for index, weight in keep:
            obj.vertex_groups[index].add([vertex.index], weight / total, "REPLACE")
    obj["surface_method"] = f"Catmull-Clark level {levels}; open boundaries creased"
    return {"boundary_edges_preserved": boundary_count, "vertices_weight_limited": pruned,
            "vertices": len(obj.data.vertices)}


def validate_meshes(objects, rig):
    for obj in objects:
        if not any(m.type == "ARMATURE" and m.object == rig for m in obj.modifiers):
            raise AssertionError("Lost armature binding")
        if not obj.data.uv_layers:
            raise AssertionError("Lost UVs")
        for vertex in obj.data.vertices:
            weights = [g.weight for g in vertex.groups]
            if not 1 <= len(weights) <= 4 or abs(sum(weights) - 1) > 2e-6:
                raise AssertionError("Invalid high-poly skin weights")
            if any(not math.isfinite(v) for v in vertex.co):
                raise AssertionError("Non-finite subdivided geometry")
    if len(rig.data.bones) != 14:
        raise AssertionError("Skeleton changed")


def configure_view(center=(0, 0, 1), distance=3):
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                space = area.spaces.active
                space.shading.type = "MATERIAL"
                space.region_3d.view_rotation = Quaternion((1, 0, 0), math.pi / 2)
                space.region_3d.view_perspective = "ORTHO"
                space.region_3d.view_location = center
                space.region_3d.view_distance = distance


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", type=Path, default=repo / "build/player-rig-comparison/default_14_bones.blend")
    parser.add_argument("--output", type=Path, default=repo / "build/player-smooth")
    parser.add_argument("--levels", type=int, choices=(1, 2, 3), default=2)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    template, output = args.template.resolve(), args.output.resolve()
    for protected in (repo / "data", repo / "assets", template.parent):
        if output == protected or protected in output.parents or output in protected.parents:
            raise ValueError("Use an independent output directory")
    source = repo / "assets/animations/manifest.json"
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (template, source)}
    output.mkdir(parents=True, exist_ok=True)
    (output / "validation.json").unlink(missing_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(template))
    bpy.context.preferences.filepaths.save_version = 0
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    rig = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    if rig.animation_data and rig.animation_data.action:
        raise ValueError("Expected a static template")
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    rig.show_in_front = False
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    before = triangle_count(meshes)
    bind = {b.name: b.matrix_local.copy() for b in rig.data.bones}
    parts = {obj.name: smooth_mesh(obj, args.levels) for obj in meshes}
    after = triangle_count(meshes)
    validate_meshes(meshes, rig)
    if after <= before or any(rig.data.bones[name].matrix_local != matrix for name, matrix in bind.items()):
        raise AssertionError("Subdivision must increase triangles without changing bind transforms")
    configure_view()
    player.select_only([rig])
    smooth_template = output / "default_14_smooth.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(smooth_template))
    glb_path = output / "default_14_smooth.glb"
    player.export_glb(glb_path, [rig] + meshes)
    report = {"preview_only": True, "levels": args.levels, "source_triangles": before,
              "smooth_triangles": after, "parts": parts, "source_sha256": hashes,
              "limitations": ["Subdivision smoothing, not sculpted facial/hand/clothing detail.",
                              "Existing low-resolution textures are unchanged.",
                              "Open part boundaries stay sharp to preserve seams."]}
    report["glb"] = rig_tools.verify_bytes(glb_path, bind.keys())
    doc, _ = glb.load_glb(glb_path)
    exported_triangles = sum(doc["accessors"][p["indices"]]["count"] // 3
                             for m in doc["meshes"] for p in m["primitives"])
    if exported_triangles != after:
        raise AssertionError("GLB did not contain the actual subdivided mesh")
    report["glb"]["triangles"] = exported_triangles
    report["glb"]["blender_reimport"] = rig_tools.verify_reimport(glb_path, 14)
    # Also provide a same-scene, same-animation low/smooth comparison.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps, scene.render.fps_base = 100, 1
    scene.frame_start, scene.frame_end = 0, 240
    clips = simanim.load(source)
    pair = []
    for suffix in ("/movement/sprint/000_idlelevel1.anim", "/movement/sprint/000_idlelevel1.anim_mirror"):
        found = [c for c in clips if c.name.endswith(suffix)]
        if len(found) != 1:
            raise ValueError("Expected sprint half-step pair")
        pair.append(found[0])
    joints = legacy.load_rig(repo / "data/media/objects/players/player.object")
    rigs = []
    for path, x, title in ((template, -0.7, f"ORIGINAL | {before} TRIS"),
                           (smooth_template, 0.7, f"SMOOTH | {after} TRIS")):
        runner, _ = comparison.append_character(path, 14, x)
        runner.name = "Original_14" if x < 0 else "Smooth_14"
        action, _ = run.make_action(runner, joints, pair, False, 5)
        action.name = runner.name + "_run"
        comparison.add_label(14, x)
        # Replace helper text with mesh counts, keeping geometry outside exports.
        text = bpy.data.objects["Label_14" if x < 0 else "Label_14.001"].data
        text.body, text.size = title, 0.10
        rigs.append(runner)
    shared_error = 0.0
    for frame in range(241):
        scene.frame_set(frame)
        for name in bind:
            a, b = (r.pose.bones[name].matrix for r in rigs)
            shared_error = max(shared_error, max(abs(a[r][c] - b[r][c]) for r in range(4) for c in range(4)))
    if shared_error > 2e-5:
        raise AssertionError("Low/smooth runners have different animation")
    report["running_comparison"] = {"frames_checked": 241, "bone_matrix_error": shared_error}
    scene.frame_set(0)
    player.select_only([rigs[1]])
    configure_view(distance=4.5)
    bpy.ops.wm.save_as_mainfile(filepath=str(output / "original_vs_smooth_run.blend"))
    for path, original in hashes.items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != original:
            raise AssertionError("Modified input assets")
    player.write_json(output / "validation.json", report)
    print("SMOOTH_PLAYER_OK", output, report, flush=True)


if __name__ == "__main__":
    main()

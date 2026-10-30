"""Split the smoothed player into swappable clothing, shoes and hair GLB packages.

All packages share assets/skeletons/player.json. This is an offline asset tool;
it does not change rigging, simulation or the complete default body package.
"""
import argparse
import hashlib
from pathlib import Path
import sys

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import animation_assets as assets
import export_player as player
import export_player_hair as hair_tools
import export_rig_comparison as rig_tools
import verify_glb as glb

KITS = {
    "home": ("databases/default/images_teams/primeradivision/fcbarcelona_kit_02.bmp",
             "Primera Division home kit"),
    "away": ("databases/default/images_teams/primeradivision/realmadrid_kit_02.bmp",
             "Primera Division away kit"),
    "goalkeeper": ("media/objects/players/textures/goalie_kit.bmp", "Goalkeeper kit"),
    "referee": ("media/objects/players/textures/referee_kit.bmp", "Referee kit"),
}
PARTS = {
    "base": ("head", "arm_left", "arm_right", "knee_left", "knee_right"),
    "shoes": ("shoe_left", "shoe_right", "sole_left", "sole_right"),
    "kit": ("body", "sock_left", "sock_right"),
}
SHOES = {"shoes": "shoe.bmp", "shoe_soles": "shoe_sole.bmp"}


def selected(rig, meshes):
    player.select_only([rig] + meshes)


def export_package(raw, rig, meshes, name):
    selected(rig, meshes)
    player.export_glb(raw / (name + ".raw.glb"), [rig] + meshes)


def canonicalize(raw, output, skeleton, name, target_name=None):
    source = raw / (name + ".raw.glb")
    target = output / ((target_name or name) + ".glb")
    assets.canonical_character(source, target, skeleton)
    source.unlink()
    return target


def verify_package(path, bones):
    result = rig_tools.verify_bytes(path, bones)
    asset = glb.load_glb(path)
    doc, _ = asset
    if doc.get("animations"):
        raise AssertionError(f"Appearance package contains animation: {path}")
    if not doc.get("images") or any("bufferView" not in image for image in doc["images"]):
        raise AssertionError(f"Appearance package lost embedded textures: {path}")
    return result


def skin_matrix(path):
    asset = glb.load_glb(path)
    doc, _ = asset
    skin = doc["skins"][0]
    worlds = glb.world_matrices(doc)
    return [worlds[node] @ glb.matrix(value) for node, value in zip(
        skin["joints"], glb.accessor(asset, skin["inverseBindMatrices"]))]


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", type=Path,
                        default=repo / "build/player-smooth/default_14_smooth.blend")
    parser.add_argument("--skeleton", type=Path, default=repo / "assets/skeletons/player.json")
    parser.add_argument("--output", type=Path, default=repo / "assets/characters")
    parser.add_argument("--build", type=Path, default=repo / "build/character-parts")
    parser.add_argument("--hair-levels", type=int, choices=(0, 1, 2), default=2)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    template, skeleton, output, raw = (args.template.resolve(), args.skeleton.resolve(),
                                       args.output.resolve(), args.build.resolve())
    data = repo / "data"
    for path in (template, skeleton):
        if not path.is_file():
            raise FileNotFoundError(path)
    for name in ("base", "shoes", "kit", "hair"):
        (output / name).mkdir(parents=True, exist_ok=True)
        (output / name / "textures").mkdir(exist_ok=True)
        (raw / name).mkdir(parents=True, exist_ok=True)
    inputs = [template, skeleton] + [data / path for path, _ in
              (value for value in KITS.values())]
    inputs += [data / "media/objects/players/textures" / name for name in SHOES.values()]
    inputs += [data / "media/objects/players/hairstyles" / (style + ".ase") for style in
               hair_tools.STYLES]
    inputs += [data / "media/objects/players/textures/hair" / (color + ".bmp") for color in
               hair_tools.COLORS]
    hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    bpy.ops.wm.open_mainfile(filepath=str(template))
    bpy.context.preferences.filepaths.save_version = 0
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    rigs = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    if len(rigs) != 1 or len(rigs[0].data.bones) != 14:
        raise ValueError("Expected the static 14-bone smoothed template")
    rig = rigs[0]
    bones = [b.name for b in rig.data.bones]
    objects = [o for o in bpy.data.objects if o.type == "MESH"]
    by_part = {}
    for obj in objects:
        by_part.setdefault(obj.get("legacy_mesh"), []).append(obj)
    if set(by_part) != {name for names in PARTS.values() for name in names}:
        raise ValueError(f"Unexpected template parts: {sorted(by_part)}")
    manifest_parts = {}
    base = [obj for name in PARTS["base"] for obj in by_part[name]]
    export_package(raw, rig, base, "base")
    base_path = canonicalize(raw, output / "base", skeleton, "base")
    manifest_parts["base"] = {"file": "base/base.glb", "slot": "skin",
                              "meshes": list(PARTS["base"])}
    # Persist the shared skin texture; GLBs embed the same PNG payload.
    skin_source = data / "media/objects/players/textures/skin01.bmp"
    player.load_texture(skin_source, output / "base/textures/skin.png")

    shoe_objects = [obj for name in PARTS["shoes"] for obj in by_part[name]]
    export_package(raw, rig, shoe_objects, "shoes")
    shoe_path = canonicalize(raw, output / "shoes", skeleton, "shoes", "default")
    manifest_parts["shoes"] = {"file": "shoes/default.glb", "slot": "shoes",
                               "meshes": list(PARTS["shoes"])}
    for source in sorted({obj.material_slots[0].material for obj in shoe_objects},
                         key=lambda material: material["mod_slot"]):
        player.load_texture(data / "media/objects/players/textures" / SHOES[source["mod_slot"]],
                            output / "shoes/textures" /
                            ({"shoes": "shoe", "shoe_soles": "shoe_sole"}[source["mod_slot"]] + ".png"))

    kit_objects = [obj for name in PARTS["kit"] for obj in by_part[name]]
    kit_material = kit_objects[0].material_slots[0].material
    image_node = kit_material.node_tree.nodes["ReplaceableBaseColor"]
    original = image_node.image
    kit_entries = {}
    try:
        for name, (relative, description) in KITS.items():
            image_node.image = player.load_texture(
                data / relative, output / "kit/textures" / (name + ".png"))
            export_package(raw, rig, kit_objects, "kit_" + name)
            path = canonicalize(raw, output / "kit", skeleton, "kit_" + name, name)
            kit_entries[name] = {"file": f"kit/{name}.glb", "slot": "kit",
                                 "description": description, "meshes": list(PARTS["kit"]),
                                 "texture": f"kit/textures/{name}.png"}
            verify_package(path, bones)
    finally:
        image_node.image = original

    hair_entries = {}
    black = player.load_texture(
        data / "media/objects/players/textures/hair/black.bmp",
        output / "hair/textures/black.png")
    for color in hair_tools.COLORS:
        if color != "black":
            player.load_texture(data / "media/objects/players/textures/hair" / (color + ".bmp"),
                                output / "hair/textures" / (color + ".png"))
    for style in hair_tools.STYLES:
        source_material = player.make_material("hair_" + style, black, "legacy_hair")
        hair, _ = hair_tools.attach_hair(
            rig, data / "media/objects/players/hairstyles" / (style + ".ase"),
            source_material, args.hair_levels)
        export_package(raw, rig, [hair], "hair_" + style)
        bpy.data.objects.remove(hair, do_unlink=True)
        path = canonicalize(raw, output / "hair", skeleton, "hair_" + style, style)
        hair_entries[style] = {"file": f"hair/{style}.glb", "slot": "hair",
                               "attachment_bone": "neck",
                               "texture": "hair/textures/black.png"}
        verify_package(path, bones)
        player.select_only([rig])

    # Every package must be bind-compatible: identical 14 skin transforms.
    reference = skin_matrix(base_path)
    packages = [base_path, shoe_path] + [output / entry["file"] for entry in
                                         list(kit_entries.values()) + list(hair_entries.values())]
    for path in packages:
        matrices = skin_matrix(path)
        if len(matrices) != 14 or max(abs(a[r][c] - b[r][c])
                for a, b in zip(reference, matrices) for r in range(4) for c in range(4)) > 2e-5:
            raise AssertionError(f"Package is not bind-compatible: {path}")
    for name, entry in list(kit_entries.items()) + list(hair_entries.items()):
        result = verify_package(output / entry["file"], bones)
        entry["mesh_parts"] = result["meshes"]
    player.write_json(output / "manifest.json", {
        "schema": "football_character_parts_v1",
        "rig_version": assets.RIG,
        "skeleton": "../skeletons/player.json",
        "units": "metres", "up_axis": "Y",
        "base": manifest_parts["base"], "shoes": manifest_parts["shoes"],
        "kits": kit_entries, "hair": hair_entries,
        "assembly": "Load one package per slot, keep a single 14-joint skeleton, "
                    "and map by bone name. Packages embed the same rest skin transforms.",
        "textures": "Embedded PNG; loose PNG copies are provided for texture mods "
                    "and do not update a GLB automatically.",
        "limitations": ["Shirt, shorts, torso and socks share one kit mesh/material; "
                        "they are not independently separable parts.",
                        "Hair is rigid geometry attached to neck; no hair simulation.",
                        "Base contains head, arms and knees only; torso and socks come "
                        "from a kit package.",
                        "No normal maps, facial features or finger/toe geometry."],
        "source_sha256": hashes})
    for path, original in hashes.items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != original:
            raise AssertionError("Modified source asset")
    print("CHARACTER_PARTS_OK", output, flush=True)



if __name__ == "__main__":
    main()

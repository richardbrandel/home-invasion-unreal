"""Re-export a character glb through Blender: one joined mesh, one armature, sane units.

    blender --background --python tools/blender-reexport.py -- <in.glb> <out.glb> [height_m]

WHY (2026-11-01). `alina.glb`'s skin is not trustworthy on its own: 21 of its 102 inverse-bind
matrices are zero-translation placeholders (measured -- see tools/fix-glb-skin.py, which repairs
them), and the engine's importer then scales joints and vertices by different factors, so no
single number in the file reconciles the two. Blender is the tool that recomputes BOTH at once: it
builds the armature from the node transforms, rebuilds the bind from that, and writes a glb whose
numbers are internally consistent. That is the whole point of going through it.

What this does:
  * imports the glb (feed it the IBM-repaired one, `fix-glb-skin.py` output -- Blender will build
    its own bind regardless, but a self-consistent input cannot hurt);
  * keeps ONE mesh object, joined from however many parts the file has (alina has 19), because the
    game and the retargeter both address a single mesh at `characters_<tag>/idle`;
  * scales the character to `height_m` METRES, applying the transform, so the size is decided here
    rather than discovered after an import;
  * exports glb (not FBX): Blender's FBX exporter is the one this project already recorded as
    broken for skinned armatures in 5.0 (`use_space_transform`), and its glTF exporter is sound.
"""

import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if len(argv) < 2:
    raise SystemExit("usage: blender --background --python blender-reexport.py -- in.glb out.glb [height_m]")
src, dst = argv[0], argv[1]
target_h = float(argv[2]) if len(argv) > 2 else 1.75

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)

meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
armatures = [o for o in bpy.context.scene.objects if o.type == "ARMATURE"]
print("REEXPORT: imported %d mesh(es), %d armature(s)" % (len(meshes), len(armatures)))
if not meshes:
    raise SystemExit("REEXPORT: no mesh imported")

# Join every part into the first mesh, keeping its armature modifier (all parts share one skeleton).
bpy.ops.object.select_all(action="DESELECT")
for m in meshes:
    m.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
body = bpy.context.view_layer.objects.active
print("REEXPORT: joined into '%s' (%d vertices)" % (body.name, len(body.data.vertices)))

# SHE IS SKINNED, so her visible size is governed by the ARMATURE, not by the mesh object's own
# transform. Measure the DEFORMED result (evaluated depsgraph) and scale the armature; scaling both
# objects made the first attempt land at 1.41 m against a 1.75 m target.
armature = armatures[0] if armatures else None
depsgraph = bpy.context.evaluated_depsgraph_get()


def deformed_height():
    bpy.context.view_layer.update()
    evaluated = body.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    zs = [(evaluated.matrix_world @ v.co).z for v in mesh.vertices]
    lo, hi = min(zs), max(zs)
    evaluated.to_mesh_clear()
    return lo, hi


lo, hi = deformed_height()
print("REEXPORT: height before = %.4f m (feet at %.4f)" % (hi - lo, lo))
if hi - lo <= 0:
    raise SystemExit("REEXPORT: zero height")
if armature is not None:
    factor = target_h / (hi - lo)
    armature.scale = [v * factor for v in armature.scale]
    bpy.context.view_layer.update()

lo, hi = deformed_height()
print("REEXPORT: height after  = %.4f m (target %.3f), feet at %.4f" % (hi - lo, target_h, lo))

# Ground her: every other character in the game has its origin at the feet, and HI.Unreal.Cast
# asserts the standing idle's Hips sit at ~96 cm rather than at 0.
if armature is not None:
    armature.location.z -= lo
bpy.context.view_layer.update()

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
lo, hi = deformed_height()
print("REEXPORT: final height %.4f m, feet at %.4f" % (hi - lo, lo))

bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", use_selection=False,
                          export_apply=True, export_animations=False, export_skins=True,
                          export_yup=True)
print("REEXPORT: wrote %s" % dst)

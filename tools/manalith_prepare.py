"""Blender step of tools/import-manalith.ps1 (US-T458): the Meshy "Manalith" model, ready for Unreal.

    blender -b --factory-startup --python tools/manalith_prepare.py -- <source folder> <out folder> [faces]

Reads the Meshy export (one .fbx and its *_texture.png, *_metallic.png, *_roughness.png, *_normal.png), decimates the
mesh to about <faces> faces (60000 by default: the export is ~58 MB), centres it on its bounding box and writes
manalith-satellite-00.fbx plus the four maps at 2048 at most (manalith-base-00.png, manalith-metallic-00.png,
manalith-roughness-00.png, manalith-normal-00.png) into <out folder>.
"""

import glob
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
src, out = argv[0], argv[1]
target = int(argv[2]) if len(argv) > 2 else 60000
os.makedirs(out, exist_ok=True)

fbx = glob.glob(os.path.join(src, "*.fbx"))
if len(fbx) != 1:
    raise SystemExit(f"MANALITH expected one .fbx in {src}, found {len(fbx)}")

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=fbx[0])
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
before = sum(len(o.data.polygons) for o in meshes)
ratio = min(1.0, target / max(1, before))
for o in meshes:
    bpy.context.view_layer.objects.active = o
    m = o.modifiers.new("dec", 'DECIMATE')
    m.ratio = ratio
    m.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier="dec")
after = sum(len(o.data.polygons) for o in meshes)

# Centre on the joint box, so the tumble turns it about its middle.
lo = Vector((1e30, 1e30, 1e30))
hi = Vector((-1e30, -1e30, -1e30))
for o in meshes:
    for c in o.bound_box:
        w = o.matrix_world @ Vector(c)
        lo = Vector(map(min, lo, w))
        hi = Vector(map(max, hi, w))
centre = (lo + hi) * 0.5
for o in meshes:
    o.location -= centre
print(f"MANALITH faces {before} -> {after}, size {tuple(round(v, 4) for v in (hi - lo))}")

for o in bpy.context.scene.objects:
    o.select_set(o.type == 'MESH')
bpy.ops.export_scene.fbx(filepath=os.path.join(out, "manalith-satellite-00.fbx"), use_selection=True, path_mode='STRIP',
                         embed_textures=False, mesh_smooth_type='FACE', add_leaf_bones=False, bake_anim=False)

maps = {"_texture.png": "manalith-base-00.png", "_metallic.png": "manalith-metallic-00.png",
        "_roughness.png": "manalith-roughness-00.png", "_normal.png": "manalith-normal-00.png"}
for suffix, name in maps.items():
    found = glob.glob(os.path.join(src, "*" + suffix))
    if not found:
        raise SystemExit(f"MANALITH missing *{suffix} in {src}")
    img = bpy.data.images.load(found[0])
    w, h = img.size
    if max(w, h) > 2048:
        k = 2048 / max(w, h)
        img.scale(int(w * k), int(h * k))
    img.filepath_raw = os.path.join(out, name)
    img.file_format = 'PNG'
    img.save()
    # Average colour of the base map, for the dark debris shards (the spec: average x 0.5).
    if name == "manalith-base-00.png":
        px = img.pixels[:]
        n = len(px) // 4
        step = max(1, n // 200000)
        r = g = b = 0.0
        k = 0
        for i in range(0, n, step):
            r += px[i * 4]; g += px[i * 4 + 1]; b += px[i * 4 + 2]; k += 1
        print(f"MANALITH base average {r / k:.4f} {g / k:.4f} {b / k:.4f}")
    print(f"MANALITH wrote {name} {img.size[0]}x{img.size[1]}")

#!/usr/bin/env python3
"""Render a glb's chest from the front and in profile, framed the same way every time.

    blender -b -P tools/render-chest.py -- <in.glb> <outdir> <tag>

The camera is placed from the MESH, not guessed: the front is the horizontal direction in which the
chest band protrudes furthest, so a model that faces an unusual way still gets a true front view. Used
for the chest A/B sheets -- the numbers a tool prints cannot tell a cup from a slab, only a picture.
"""
import math
import os
import sys

import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
glb, outdir, tag = argv[0], argv[1], argv[2]
os.makedirs(outdir, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb)

meshes = [o for o in bpy.data.objects if o.type == "MESH"]
if not meshes:
    raise SystemExit("no meshes in %s" % glb)

# World-space vertices of everything, welded into one cloud.
buf = []
for o in meshes:
    m = o.matrix_world
    for v in o.data.vertices:
        buf.append(m @ v.co)
V = np.array([[p.x, p.y, p.z] for p in buf], dtype=float)
lo, hi = V.min(axis=0), V.max(axis=0)
H = hi[2] - lo[2]
centre = V.mean(axis=0)

# The chest band, and the horizontal direction it protrudes furthest in.
band = V[(V[:, 2] > lo[2] + 0.64 * H) & (V[:, 2] < lo[2] + 0.78 * H)]
mid = np.array([band[:, 0].mean(), band[:, 1].mean(), 0.0])
ang, reach = 0.0, -1e9
for k in range(72):
    a = 2 * math.pi * k / 72
    d = np.array([math.cos(a), math.sin(a), 0.0])
    r = float(np.max((band[:, :2] - mid[:2]) @ d[:2]))
    if r > reach:
        ang, reach = a, r
front = np.array([math.cos(ang), math.sin(ang), 0.0])
side = np.array([-front[1], front[0], 0.0])

# A neutral grey studio, so the silhouette is what the eye reads.
world = bpy.data.worlds.new("W")
bpy.context.scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.55, 0.55, 1.0)
world.node_tree.nodes["Background"].inputs[1].default_value = 1.0

key = bpy.data.lights.new("Key", type="SUN")
key.energy = 3.0
key_obj = bpy.data.objects.new("Key", key)
bpy.context.collection.objects.link(key_obj)
key_obj.rotation_euler = (math.radians(55), 0.0, ang + math.radians(35))

cam_data = bpy.data.cameras.new("Cam")
cam_data.type = "ORTHO"
cam_data.ortho_scale = H * 0.62
cam = bpy.data.objects.new("Cam", cam_data)
bpy.context.collection.objects.link(cam)
bpy.context.scene.camera = cam

target = np.array([centre[0], centre[1], lo[2] + 0.74 * H])
scene = bpy.context.scene
scene.render.resolution_x = 900
scene.render.resolution_y = 1200
scene.render.film_transparent = False
scene.render.image_settings.file_format = "PNG"
try:
    scene.render.engine = "BLENDER_EEVEE"
except TypeError:
    scene.render.engine = "BLENDER_EEVEE_NEXT"


def shoot(direction, name):
    d = direction / np.linalg.norm(direction)
    pos = target + d * (H * 2.5) + np.array([0.0, 0.0, 0.02 * H])
    cam.location = (pos[0], pos[1], pos[2])
    # look at the target
    fwd = target - pos
    yaw = math.atan2(fwd[1], fwd[0])
    pitch = math.acos(fwd[2] / np.linalg.norm(fwd))
    cam.rotation_mode = "XYZ"
    cam.rotation_euler = (pitch, 0.0, yaw - math.pi / 2)
    scene.render.filepath = os.path.join(outdir, "%s_%s.png" % (tag, name))
    bpy.ops.render.render(write_still=True)
    print("RENDER: %s" % scene.render.filepath)


shoot(front, "front")
shoot(side, "side")
shoot(front * -1.0, "back")
shoot(-side, "side2")
print("RENDER: front direction (%.2f, %.2f), H=%.1f" % (front[0], front[1], H))

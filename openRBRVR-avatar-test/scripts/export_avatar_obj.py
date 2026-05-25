import sys
import bpy

argv = sys.argv
if "--" not in argv:
    raise SystemExit("usage: blender --background file.blend --python export_avatar_obj.py -- output.obj")

out_path = argv[argv.index("--") + 1]

for obj in bpy.context.scene.objects:
    obj.select_set(obj.type == "MESH")

bpy.ops.wm.obj_export(
    filepath=out_path,
    export_selected_objects=True,
    export_materials=True,
    export_uv=True,
    forward_axis="Y",
    up_axis="Z",
)

"""
panels.py
Blender UI パネル定義
"""

import bpy


class FCV_PT_MainPanel(bpy.types.Panel):
    bl_label       = "Fluid Cache Viewer"
    bl_idname      = "FCV_PT_main"
    bl_space_type  = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context     = "scene"

    def draw(self, context):
        layout = self.layout
        scene  = context.scene

        # ---- Cache Settings ----
        box = layout.box()
        box.label(text="Cache Settings", icon="FILE_FOLDER")
        box.prop(scene, "fcv_cache_dir")
        row = box.row()
        row.prop(scene, "fcv_frame_start")
        row.prop(scene, "fcv_frame_end")

        # ---- Optimization ----
        box = layout.box()
        box.label(text="Optimization", icon="MOD_FLUID")
        box.prop(scene, "fcv_threshold")
        box.prop(scene, "fcv_lod_level")

        # ---- Controls ----
        row = layout.row(align=True)
        row.scale_y = 1.4
        row.operator("fcv.load_cache",   text="Load",   icon="PLAY")
        row.operator("fcv.unload_cache", text="Unload", icon="X")

        # ---- Export ----
        layout.operator("fcv.export_openvdb", text="Export OpenVDB", icon="EXPORT")

        # ---- Stats ----
        box = layout.box()
        box.label(text="Stats", icon="INFO")
        box.label(text=scene.fcv_stats)


CLASSES = [FCV_PT_MainPanel]

def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)

def unregister():
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)

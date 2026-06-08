bl_info = {
    "name":        "Fluid Cache Viewer",
    "author":      "QuickDraw Study Project",
    "version":     (1, 0, 0),
    "blender":     (3, 6, 0),
    "location":    "Properties > Scene > Fluid Cache Viewer",
    "description": "Jet .vol fluid cache loader with dirty-rect and BVH optimizations",
    "category":    "Import-Export",
}

import bpy
from . import operators, panels

def register():
    operators.register()
    panels.register()

    # Scene properties
    bpy.types.Scene.fcv_cache_dir = bpy.props.StringProperty(
        name        = "Cache Directory",
        description = "Directory containing .vol frame files",
        subtype     = "DIR_PATH",
    )
    bpy.types.Scene.fcv_frame_start = bpy.props.IntProperty(
        name    = "Start Frame",
        default = 0,
        min     = 0,
    )
    bpy.types.Scene.fcv_frame_end = bpy.props.IntProperty(
        name    = "End Frame",
        default = 100,
        min     = 0,
    )
    bpy.types.Scene.fcv_threshold = bpy.props.FloatProperty(
        name        = "Dirty Threshold",
        description = "Voxel change threshold for dirty-rect detection",
        default     = 0.01,
        min         = 0.0,
        max         = 1.0,
    )
    bpy.types.Scene.fcv_lod_level = bpy.props.EnumProperty(
        name  = "LOD Level",
        items = [
            ("0", "Full",    "Full resolution"),
            ("1", "Half",    "1/2 resolution"),
            ("2", "Quarter", "1/4 resolution"),
        ],
        default = "0",
    )
    bpy.types.Scene.fcv_stats = bpy.props.StringProperty(
        name    = "Stats",
        default = "---",
    )

def unregister():
    operators.unregister()
    panels.unregister()

    del bpy.types.Scene.fcv_cache_dir
    del bpy.types.Scene.fcv_frame_start
    del bpy.types.Scene.fcv_frame_end
    del bpy.types.Scene.fcv_threshold
    del bpy.types.Scene.fcv_lod_level
    del bpy.types.Scene.fcv_stats

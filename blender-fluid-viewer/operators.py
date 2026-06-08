"""
operators.py
Blender オペレーター定義
"""

import bpy
import numpy as np
import time
from .cache_manager import get_manager, reset_manager, AABB


# ================================================================
# フレーム変更ハンドラー
# Blender のタイムライン再生時に自動的に呼び出される
# ================================================================
_handler_registered = False

def _on_frame_change(scene, depsgraph=None):
    """
    フレームが変わるたびに呼ばれる。
    ダーティレクトで変化ボクセルのみを更新。
    """
    mgr       = get_manager()
    cache_dir = scene.fcv_cache_dir
    threshold = scene.fcv_threshold
    lod       = int(scene.fcv_lod_level)
    frame_idx = scene.frame_current

    if not cache_dir:
        return

    # フレームデータ読み込み（LRU キャッシュ優先）
    vol = mgr.load_frame(cache_dir, frame_idx, threshold, lod)
    if vol is None:
        scene.fcv_stats = f"Frame {frame_idx}: file not found"
        return

    # BVH 構築（解像度変化時のみ再構築）
    mgr.build_bvh(vol)

    # ダーティレクト: 変化ボクセルのみ取得
    mask = mgr.compute_dirty(vol, threshold)

    # Blender オブジェクトを更新
    _update_blender_object(scene, vol, mask)

    # 統計情報をパネルに表示
    scene.fcv_stats = mgr.stats_string()


def _update_blender_object(scene, vol, dirty_mask):
    """
    VolFrame データを Blender の Mesh オブジェクト（点群）として表示。
    dirty_mask が True のボクセルのみを更新。
    """
    obj_name = "FluidCache"
    mesh_name = "FluidCacheMesh"

    # ダーティなボクセルの座標を取得
    changed = np.argwhere(dirty_mask)   # shape: (N, 3)
    if changed.size == 0:
        return   # 変化なし -> 更新スキップ（最適化の核心）

    # 密度でフィルタリング（空のボクセルを除外）
    header = vol.header
    step_x = (header.aabb_max[0] - header.aabb_min[0]) / header.x_res
    step_y = (header.aabb_max[1] - header.aabb_min[1]) / header.y_res
    step_z = (header.aabb_max[2] - header.aabb_min[2]) / header.z_res

    # 変化ボクセルの密度値
    densities = vol.data[changed[:, 0], changed[:, 1], changed[:, 2]]
    active    = densities > 0.001
    changed   = changed[active]

    if changed.size == 0:
        return

    # ワールド座標に変換
    coords = np.column_stack([
        header.aabb_min[0] + changed[:, 0] * step_x,
        header.aabb_min[1] + changed[:, 1] * step_y,
        header.aabb_min[2] + changed[:, 2] * step_z,
    ])

    # Mesh オブジェクトの作成または取得
    if obj_name in bpy.data.objects:
        obj  = bpy.data.objects[obj_name]
        mesh = obj.data
    else:
        mesh = bpy.data.meshes.new(mesh_name)
        obj  = bpy.data.objects.new(obj_name, mesh)
        bpy.context.scene.collection.objects.link(obj)

    # メッシュを点群として更新
    vertices = [tuple(c) for c in coords]
    mesh.clear_geometry()
    mesh.from_pydata(vertices, [], [])
    mesh.update()


# ================================================================
# OT_LoadCache - キャッシュ読み込みオペレーター
# ================================================================
class FCV_OT_LoadCache(bpy.types.Operator):
    bl_idname  = "fcv.load_cache"
    bl_label   = "Load Cache"
    bl_description = "Load fluid cache and register frame handler"

    def execute(self, context):
        global _handler_registered

        # マネージャーをリセット
        reset_manager()

        # フレーム変更ハンドラーを登録
        if not _handler_registered:
            bpy.app.handlers.frame_change_post.append(_on_frame_change)
            _handler_registered = True

        # 現在フレームを即時反映
        _on_frame_change(context.scene)

        self.report({"INFO"}, "Fluid cache loaded")
        return {"FINISHED"}


# ================================================================
# OT_UnloadCache - キャッシュ解放オペレーター
# ================================================================
class FCV_OT_UnloadCache(bpy.types.Operator):
    bl_idname  = "fcv.unload_cache"
    bl_label   = "Unload Cache"
    bl_description = "Release cache and remove frame handler"

    def execute(self, context):
        global _handler_registered

        if _handler_registered:
            if _on_frame_change in bpy.app.handlers.frame_change_post:
                bpy.app.handlers.frame_change_post.remove(_on_frame_change)
            _handler_registered = False

        reset_manager()
        context.scene.fcv_stats = "---"

        # オブジェクト削除
        if "FluidCache" in bpy.data.objects:
            bpy.data.objects.remove(bpy.data.objects["FluidCache"], do_unlink=True)

        self.report({"INFO"}, "Cache unloaded")
        return {"FINISHED"}


# ================================================================
# OT_ExportOpenVDB - .vol -> OpenVDB 変換オペレーター
# ================================================================
class FCV_OT_ExportOpenVDB(bpy.types.Operator):
    bl_idname  = "fcv.export_openvdb"
    bl_label   = "Export OpenVDB"
    bl_description = "Convert current frame .vol to OpenVDB (.vdb)"

    filepath: bpy.props.StringProperty(subtype="FILE_PATH")

    def invoke(self, context, event):
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        scene     = context.scene
        cache_dir = scene.fcv_cache_dir
        frame_idx = scene.frame_current
        lod       = int(scene.fcv_lod_level)

        mgr = get_manager()
        vol = mgr.load_frame(cache_dir, frame_idx, 0.0, lod)
        if vol is None:
            self.report({"ERROR"}, "No frame data loaded")
            return {"CANCELLED"}

        try:
            import pyopenvdb as vdb
            grid = vdb.FloatGrid()
            grid.name = "density"
            accessor = grid.getAccessor()
            h = vol.header
            it = np.nditer(vol.data, flags=["multi_index"])
            while not it.finished:
                v = float(it[0])
                if v > 0.001:
                    ix, iy, iz = it.multi_index
                    accessor.setValueOn((ix, iy, iz), v)
                it.iternext()
            vdb.write(self.filepath, [grid])
            self.report({"INFO"}, f"Exported: {self.filepath}")
        except ImportError:
            self.report({"WARNING"}, "pyopenvdb not available. Install it to enable VDB export.")

        return {"FINISHED"}


# ================================================================
# Registration
# ================================================================
CLASSES = [
    FCV_OT_LoadCache,
    FCV_OT_UnloadCache,
    FCV_OT_ExportOpenVDB,
]

def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)

def unregister():
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)

    global _handler_registered
    if _handler_registered:
        if _on_frame_change in bpy.app.handlers.frame_change_post:
            bpy.app.handlers.frame_change_post.remove(_on_frame_change)
        _handler_registered = False

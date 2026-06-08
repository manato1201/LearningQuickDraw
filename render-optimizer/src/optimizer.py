"""
optimizer.py
描画最適化エンジン本体

全最適化レイヤーを統合:
  1. BVH カリング      (QuickDraw Ch3: SetClip)
  2. LOD 選択         (QuickDraw Ch5: 固定小数点最適化の思想)
  3. ダーティレクト    (QuickDraw Ch3: RegionClipping)
  4. フレーム LRU      (メモリ効率)
"""

import numpy as np
import time
from collections import OrderedDict
from dataclasses import dataclass, field
from typing import Optional, List, Dict, Any, Callable

from .aabb    import AABB, Frustum
from .bvh     import BVHTree
from .lod     import LODSelector, VoxelLOD


# ================================================================
# OptimizeResult - 1フレームの最適化結果
# ================================================================
@dataclass
class OptimizeResult:
    visible_items:  List[Any]          # BVH カリング後の可視アイテム
    lod_map:        Dict[Any, int]     # アイテム -> LOD レベル
    dirty_mask:     Optional[np.ndarray]  # ダーティボクセルマスク
    stats:          "OptimizeStats"


@dataclass
class OptimizeStats:
    total_items:    int   = 0
    visible_items:  int   = 0
    culled_items:   int   = 0
    cull_ratio:     float = 0.0
    dirty_ratio:    float = 0.0    # 変化ボクセルの割合（1.0=全変化）
    skip_ratio:     float = 0.0    # スキップされたボクセルの割合
    bvh_ms:         float = 0.0    # BVH クエリ時間
    dirty_ms:       float = 0.0    # ダーティレクト計算時間
    total_ms:       float = 0.0

    def summary(self) -> str:
        return (
            f"Visible: {self.visible_items}/{self.total_items} "
            f"(Culled: {self.cull_ratio*100:.1f}%)  |  "
            f"Dirty skip: {self.skip_ratio*100:.1f}%  |  "
            f"BVH: {self.bvh_ms:.2f}ms  Dirty: {self.dirty_ms:.2f}ms  "
            f"Total: {self.total_ms:.2f}ms"
        )


# ================================================================
# RenderOptimizer
# ================================================================
class RenderOptimizer:
    """
    汎用描画最適化エンジン。
    VDB / Alembic / .vol / 任意ジオメトリに対して
    BVH カリング + LOD + ダーティレクトを一括適用する。
    """

    def __init__(self,
                 max_cache_frames: int = 10,
                 lod_schedule=None):
        self.bvh          = BVHTree(max_leaf_items=4)
        self.lod_selector = LODSelector(lod_schedule)
        self._lru:        OrderedDict = OrderedDict()
        self._max_frames  = max_cache_frames
        self._prev_data:  Optional[np.ndarray] = None
        self._dirty_thresh = 0.01

    # ----------------------------------------------------------------
    # BVH 構築
    # ----------------------------------------------------------------
    def build_bvh(self, items: List[Any], get_aabb: Callable) -> None:
        """
        items のリストから BVH を構築する。
        items は任意オブジェクト。get_aabb(item) -> AABB を渡す。
        """
        self.bvh.build(items, get_aabb)

    # ----------------------------------------------------------------
    # メイン最適化パイプライン
    # ----------------------------------------------------------------
    def optimize(self,
                 camera_pos:   np.ndarray,
                 camera_dir:   np.ndarray,
                 camera_up:    np.ndarray,
                 fov_deg:      float,
                 aspect:       float,
                 near:         float,
                 far:          float,
                 volume_data:  Optional[np.ndarray] = None,
                 dirty_thresh: float = 0.01) -> OptimizeResult:
        """
        1フレーム分の最適化を実行して結果を返す。
        """
        t_total = time.perf_counter()
        stats   = OptimizeStats()

        # --- Step 1: 視錐台カリング (BVH) ---
        t0 = time.perf_counter()
        frustum = Frustum.from_camera(
            camera_pos, camera_dir, camera_up,
            fov_deg, aspect, near, far
        )
        if self.bvh.root:
            bvh_stats      = self.bvh.stats()
            stats.total_items   = bvh_stats["items"]
            visible        = self.bvh.query_frustum(frustum)
            stats.visible_items = len(visible)
            stats.culled_items  = stats.total_items - stats.visible_items
            stats.cull_ratio    = (stats.culled_items / stats.total_items
                                   if stats.total_items > 0 else 0.0)
        else:
            visible = []

        stats.bvh_ms = (time.perf_counter() - t0) * 1000.0

        # --- Step 2: LOD 選択 ---
        lod_map = {}
        for item in visible:
            item_aabb = self.bvh.query_aabb(
                AABB(min_pt=np.array([-1e30]*3), max_pt=np.array([1e30]*3))
            )
            # アイテムの AABB をキャッシュから再取得
            lod = 0
            if hasattr(item, "aabb"):
                lod = self.lod_selector.select(camera_pos, item.aabb)
            lod_map[id(item)] = lod

        # --- Step 3: ダーティレクト ---
        dirty_mask = None
        t0 = time.perf_counter()
        if volume_data is not None:
            dirty_mask, skip = self._compute_dirty(volume_data, dirty_thresh)
            stats.skip_ratio  = skip
            stats.dirty_ratio = 1.0 - skip
        stats.dirty_ms = (time.perf_counter() - t0) * 1000.0

        stats.total_ms = (time.perf_counter() - t_total) * 1000.0

        return OptimizeResult(
            visible_items=visible,
            lod_map=lod_map,
            dirty_mask=dirty_mask,
            stats=stats,
        )

    def _compute_dirty(self, data: np.ndarray,
                       threshold: float):
        """
        ダーティレクト計算。
        変化ボクセルのマスクとスキップ率を返す。
        """
        if self._prev_data is None or self._prev_data.shape != data.shape:
            self._prev_data = data.copy()
            return np.ones(data.shape, dtype=bool), 0.0

        diff  = np.abs(data - self._prev_data) > threshold
        skip  = 1.0 - diff.sum() / diff.size
        self._prev_data = data.copy()
        return diff, float(skip)

    # ----------------------------------------------------------------
    # LRU フレームキャッシュ
    # ----------------------------------------------------------------
    def cache_frame(self, key: Any, data: Any) -> None:
        if key in self._lru:
            self._lru.move_to_end(key)
        else:
            if len(self._lru) >= self._max_frames:
                self._lru.popitem(last=False)
            self._lru[key] = data

    def get_cached(self, key: Any) -> Optional[Any]:
        if key in self._lru:
            self._lru.move_to_end(key)
            return self._lru[key]
        return None

    def reset(self) -> None:
        self._prev_data = None
        self._lru.clear()

    def bvh_stats(self) -> dict:
        return self.bvh.stats()

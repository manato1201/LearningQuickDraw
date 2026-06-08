"""
cache_manager.py
フレームキャッシュ管理 + ダーティレクト最適化 + BVH カリング

QuickDraw との対応:
  DirtyTracker  -> QuickDraw Chapter3: RegionClipping
                   「変化した領域だけ再描画する」思想
  BVHCuller     -> QuickDraw Chapter3: SetClip
                   「見えない領域は描かない」思想
  LRUCache      -> QuickDraw Chapter5: 固定小数点最適化
                   「計算コストを最小化する」思想
"""

import numpy as np
import time
from collections import OrderedDict
from dataclasses import dataclass, field
from typing import Optional, List, Tuple
from .vol_reader import VolFrame, read_vol, vol_frame_path


# ================================================================
# DirtyTracker
# 前フレームとの差分を検出して変化ボクセルのみを更新対象にする。
# QuickDraw Chapter3: ScrollRect / RegionClipping の概念と同じ。
# ================================================================
class DirtyTracker:
    def __init__(self, threshold: float = 0.01):
        self.threshold   = threshold
        self._prev_data: Optional[np.ndarray] = None

        # Stats
        self.total_voxels   = 0
        self.changed_voxels = 0
        self.skip_ratio     = 0.0

    def update(self, frame: VolFrame) -> np.ndarray:
        """
        前フレームとの差分を計算し、変化ボクセルのマスクを返す。
        マスク True = 変化あり（再描画が必要）
        マスク False = 変化なし（スキップ可能）
        """
        data = frame.data
        self.total_voxels = data.size

        if self._prev_data is None or self._prev_data.shape != data.shape:
            # 初回 or 解像度変化 -> 全ボクセルを更新
            self._prev_data     = data.copy()
            self.changed_voxels = self.total_voxels
            self.skip_ratio     = 0.0
            return np.ones(data.shape, dtype=bool)

        # 差分計算（numpy ベクトル演算で高速処理）
        diff    = np.abs(data - self._prev_data)
        mask    = diff > self.threshold

        self.changed_voxels = int(mask.sum())
        self.skip_ratio     = 1.0 - (self.changed_voxels / self.total_voxels)

        self._prev_data = data.copy()
        return mask

    def reset(self):
        self._prev_data = None


# ================================================================
# AABB - 軸並行バウンディングボックス
# ================================================================
@dataclass
class AABB:
    min_pt: np.ndarray   # shape (3,)
    max_pt: np.ndarray   # shape (3,)

    def intersects(self, other: "AABB") -> bool:
        return (
            self.min_pt[0] <= other.max_pt[0] and self.max_pt[0] >= other.min_pt[0] and
            self.min_pt[1] <= other.max_pt[1] and self.max_pt[1] >= other.min_pt[1] and
            self.min_pt[2] <= other.max_pt[2] and self.max_pt[2] >= other.min_pt[2]
        )

    def contains_point(self, pt: np.ndarray) -> bool:
        return np.all(pt >= self.min_pt) and np.all(pt <= self.max_pt)


# ================================================================
# BVHNode - 再帰的バウンディングボリューム階層ノード
# QuickDraw Chapter3: RegionClipping の3D版
# 「視野外のブロックはまるごとスキップ」
# ================================================================
@dataclass
class BVHNode:
    aabb:     AABB
    children: List["BVHNode"] = field(default_factory=list)
    # 葉ノードのみ: ボクセルブロックの範囲
    voxel_slice: Optional[Tuple] = None   # (x0,x1, y0,y1, z0,z1)


class BVHCuller:
    def __init__(self, block_size: int = 8):
        """
        block_size: ひとつの葉ノードに含めるボクセル数（辺あたり）
        """
        self.block_size = block_size
        self._root: Optional[BVHNode] = None
        self._frame_shape: Optional[Tuple] = None

    def build(self, frame: VolFrame):
        """
        VolFrame の形状から BVH を構築する。
        解像度が変わっていなければ再構築しない。
        """
        shape = (frame.header.x_res, frame.header.y_res, frame.header.z_res)
        if shape == self._frame_shape:
            return   # 変化なし -> 再構築不要

        self._frame_shape = shape
        aabb_min = np.array(frame.header.aabb_min, dtype=np.float32)
        aabb_max = np.array(frame.header.aabb_max, dtype=np.float32)
        self._root = self._build_node(0, shape[0], 0, shape[1], 0, shape[2],
                                      aabb_min, aabb_max)

    def _build_node(self, x0, x1, y0, y1, z0, z1,
                    aabb_min, aabb_max) -> BVHNode:
        node = BVHNode(aabb=AABB(min_pt=aabb_min, max_pt=aabb_max))

        sx = x1 - x0
        sy = y1 - y0
        sz = z1 - z0

        if sx <= self.block_size and sy <= self.block_size and sz <= self.block_size:
            # 葉ノード
            node.voxel_slice = (x0, x1, y0, y1, z0, z1)
            return node

        # 最も長い辺で分割
        mx = (aabb_min + aabb_max) * 0.5

        if sx >= sy and sx >= sz:
            # X分割
            xm = (x0 + x1) // 2
            left_max  = np.array([mx[0], aabb_max[1], aabb_max[2]])
            right_min = np.array([mx[0], aabb_min[1], aabb_min[2]])
            node.children.append(self._build_node(x0, xm, y0, y1, z0, z1, aabb_min, left_max))
            node.children.append(self._build_node(xm, x1, y0, y1, z0, z1, right_min, aabb_max))
        elif sy >= sz:
            # Y分割
            ym = (y0 + y1) // 2
            left_max  = np.array([aabb_max[0], mx[1], aabb_max[2]])
            right_min = np.array([aabb_min[0], mx[1], aabb_min[2]])
            node.children.append(self._build_node(x0, x1, y0, ym, z0, z1, aabb_min, left_max))
            node.children.append(self._build_node(x0, x1, ym, y1, z0, z1, right_min, aabb_max))
        else:
            # Z分割
            zm = (z0 + z1) // 2
            left_max  = np.array([aabb_max[0], aabb_max[1], mx[2]])
            right_min = np.array([aabb_min[0], aabb_min[1], mx[2]])
            node.children.append(self._build_node(x0, x1, y0, y1, z0, zm, aabb_min, left_max))
            node.children.append(self._build_node(x0, x1, y0, y1, zm, z1, right_min, aabb_max))

        return node

    def cull(self, camera_aabb: AABB) -> List[Tuple]:
        """
        カメラ AABB と交差する葉ノードのボクセルスライスを返す。
        交差しないノードは丸ごとスキップ。
        """
        if self._root is None:
            return []
        result = []
        self._traverse(self._root, camera_aabb, result)
        return result

    def _traverse(self, node: BVHNode, cam: AABB, result: List):
        if not node.aabb.intersects(cam):
            return   # カリング: このノード以下をスキップ
        if node.voxel_slice is not None:
            result.append(node.voxel_slice)
            return
        for child in node.children:
            self._traverse(child, cam, result)


# ================================================================
# LRUFrameCache
# フレームデータを LRU キャッシュで保持。
# メモリを節約しながら頻繁にアクセスするフレームを高速再利用。
# ================================================================
class LRUFrameCache:
    def __init__(self, max_frames: int = 10):
        self._cache: OrderedDict[int, VolFrame] = OrderedDict()
        self._max   = max_frames

    def get(self, frame_idx: int) -> Optional[VolFrame]:
        if frame_idx in self._cache:
            self._cache.move_to_end(frame_idx)
            return self._cache[frame_idx]
        return None

    def put(self, frame_idx: int, frame: VolFrame):
        if frame_idx in self._cache:
            self._cache.move_to_end(frame_idx)
        else:
            if len(self._cache) >= self._max:
                self._cache.popitem(last=False)   # 最も古いフレームを破棄
            self._cache[frame_idx] = frame

    def clear(self):
        self._cache.clear()

    @property
    def size(self) -> int:
        return len(self._cache)


# ================================================================
# CacheManager - 全体を統括するマネージャー
# ================================================================
class CacheManager:
    def __init__(self):
        self.dirty_tracker = DirtyTracker()
        self.bvh_culler    = BVHCuller()
        self.lru_cache     = LRUFrameCache(max_frames=10)

        # Stats
        self.last_load_ms  = 0.0
        self.last_dirty_ms = 0.0

    def load_frame(self, cache_dir: str, frame_idx: int,
                   threshold: float, lod: int) -> Optional[VolFrame]:
        """
        フレームを読み込む。LRU キャッシュを優先的に使用。
        lod: 0=full, 1=half, 2=quarter
        """
        # キャッシュヒット確認
        cached = self.lru_cache.get(frame_idx)
        if cached is not None:
            return cached

        # ファイルから読み込み
        t0   = time.perf_counter()
        path = vol_frame_path(cache_dir, frame_idx)
        vol  = read_vol(path)
        self.last_load_ms = (time.perf_counter() - t0) * 1000.0

        if vol is None:
            return None

        # LOD ダウンサンプリング
        if lod > 0:
            vol = self._downsample(vol, lod)

        self.lru_cache.put(frame_idx, vol)
        return vol

    def compute_dirty(self, frame: VolFrame, threshold: float) -> np.ndarray:
        """
        ダーティレクト計算。変化ボクセルのマスクを返す。
        """
        t0 = time.perf_counter()
        self.dirty_tracker.threshold = threshold
        mask = self.dirty_tracker.update(frame)
        self.last_dirty_ms = (time.perf_counter() - t0) * 1000.0
        return mask

    def build_bvh(self, frame: VolFrame):
        self.bvh_culler.build(frame)

    def get_visible_slices(self, camera_aabb: AABB):
        return self.bvh_culler.cull(camera_aabb)

    def reset(self):
        self.dirty_tracker.reset()
        self.lru_cache.clear()

    def stats_string(self) -> str:
        dt  = self.dirty_tracker
        lru = self.lru_cache
        return (
            f"Load: {self.last_load_ms:.1f}ms | "
            f"Dirty: {self.last_dirty_ms:.1f}ms | "
            f"Skip: {dt.skip_ratio*100:.1f}% | "
            f"Cache: {lru.size} frames"
        )

    @staticmethod
    def _downsample(vol: VolFrame, lod: int) -> VolFrame:
        """
        ボクセルグリッドを 2^lod 倍にダウンサンプリング。
        numpy ストライドスライスで高速処理。
        """
        from .vol_reader import VolHeader
        step = 2 ** lod
        d    = vol.data[::step, ::step, ::step]
        h    = vol.header
        new_header = VolHeader(
            x_res      = d.shape[0],
            y_res      = d.shape[1],
            z_res      = d.shape[2],
            n_channels = h.n_channels,
            aabb_min   = h.aabb_min,
            aabb_max   = h.aabb_max,
        )
        from .vol_reader import VolFrame as VF
        return VF(header=new_header, data=d.copy())


# モジュールレベルのシングルトン
_manager: Optional[CacheManager] = None

def get_manager() -> CacheManager:
    global _manager
    if _manager is None:
        _manager = CacheManager()
    return _manager

def reset_manager():
    global _manager
    _manager = None

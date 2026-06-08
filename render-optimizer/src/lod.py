"""
lod.py
LOD (Level of Detail) 管理

カメラからの距離に応じてジオメトリの解像度を切り替える。
QuickDraw Ch5: 0502DDALineSpeed.p の「描画コストを計測して最小化」思想。
"""

import numpy as np
from dataclasses import dataclass
from typing import List, Callable, Any, Optional
from .aabb import AABB


@dataclass
class LODLevel:
    """ひとつの LOD レベルの定義"""
    level:       int     # 0=最高品質, 1=中, 2=低, ...
    max_dist:    float   # このレベルを使う最大カメラ距離
    description: str     # "Full", "Half", "Quarter" etc.


# デフォルトの LOD スケジュール（距離ベース）
DEFAULT_LOD_SCHEDULE = [
    LODLevel(0,  10.0,  "Full (LOD0)"),
    LODLevel(1,  30.0,  "Half (LOD1)"),
    LODLevel(2,  80.0,  "Quarter (LOD2)"),
    LODLevel(3,  999.0, "Eighth (LOD3)"),
]


class LODSelector:
    """
    カメラ位置と AABB の距離から適切な LOD レベルを選択する。
    """
    def __init__(self, schedule: List[LODLevel] = None):
        self.schedule = schedule or DEFAULT_LOD_SCHEDULE
        # 距離順にソート
        self.schedule.sort(key=lambda l: l.max_dist)

    def select(self, camera_pos: np.ndarray, item_aabb: AABB) -> int:
        """
        カメラ位置と AABB の最近点距離から LOD レベルを決定。
        距離が近い -> LOD0(高品質), 遠い -> LOD3(低品質)
        """
        dist = self._dist_to_aabb(camera_pos, item_aabb)
        for level in self.schedule:
            if dist <= level.max_dist:
                return level.level
        return self.schedule[-1].level

    @staticmethod
    def _dist_to_aabb(pt: np.ndarray, aabb: AABB) -> float:
        """点からAABBまでの最短距離"""
        clamped = np.clip(pt, aabb.min_pt, aabb.max_pt)
        return float(np.linalg.norm(pt - clamped))


class VoxelLOD:
    """
    ボクセルグリッドの LOD ダウンサンプリング。
    numpy ストライドスライスで O(1) に近い速度で実現。

    QuickDraw Ch5: 固定小数点最適化と同じ思想
    「精度を落として処理量を劇的に削減する」
    """

    @staticmethod
    def downsample(data: np.ndarray, lod: int) -> np.ndarray:
        """
        3D ボクセル配列を 2^lod 倍にダウンサンプリング。
        lod=1: 1/2 -> 処理量 1/8
        lod=2: 1/4 -> 処理量 1/64
        lod=3: 1/8 -> 処理量 1/512
        """
        if lod <= 0:
            return data
        step = 2 ** lod
        return data[::step, ::step, ::step]

    @staticmethod
    def cost_ratio(lod: int) -> float:
        """LOD レベルでの処理量の割合（1.0 = フル解像度）"""
        if lod <= 0:
            return 1.0
        step = 2 ** lod
        return 1.0 / (step ** 3)


class MeshLOD:
    """
    メッシュジオメトリの LOD 管理。
    複数解像度のメッシュを保持し、距離に応じて切り替える。
    Alembic キャッシュに対して適用。
    """

    def __init__(self):
        self._levels: dict = {}   # lod_level -> mesh_data

    def add_level(self, lod: int, mesh_data: Any):
        self._levels[lod] = mesh_data

    def get(self, lod: int) -> Optional[Any]:
        # 指定レベルがなければ最近い低品質を返す
        for level in sorted(self._levels.keys()):
            if level >= lod:
                return self._levels[level]
        return self._levels.get(max(self._levels.keys()))

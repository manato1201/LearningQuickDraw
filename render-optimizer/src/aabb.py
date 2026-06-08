"""
aabb.py
軸並行バウンディングボックス (Axis-Aligned Bounding Box)

QuickDraw Ch4: SectRect / UnionRect / PtInRect の3D版。
矩形演算の基礎がそのままAABB演算に対応する。
"""

import numpy as np
from dataclasses import dataclass
from typing import List, Optional


@dataclass
class AABB:
    min_pt: np.ndarray   # shape (3,) float32
    max_pt: np.ndarray   # shape (3,) float32

    # ----------------------------------------------------------------
    # QuickDraw Ch4: SectRect 相当
    # 2つの AABB が交差しているか
    # ----------------------------------------------------------------
    def intersects(self, other: "AABB") -> bool:
        return (
            self.min_pt[0] <= other.max_pt[0] and self.max_pt[0] >= other.min_pt[0] and
            self.min_pt[1] <= other.max_pt[1] and self.max_pt[1] >= other.min_pt[1] and
            self.min_pt[2] <= other.max_pt[2] and self.max_pt[2] >= other.min_pt[2]
        )

    # ----------------------------------------------------------------
    # QuickDraw Ch4: UnionRect 相当
    # 2つの AABB を包む最小 AABB
    # ----------------------------------------------------------------
    def union(self, other: "AABB") -> "AABB":
        return AABB(
            min_pt=np.minimum(self.min_pt, other.min_pt),
            max_pt=np.maximum(self.max_pt, other.max_pt),
        )

    # ----------------------------------------------------------------
    # QuickDraw Ch4: PtInRect 相当
    # 点が AABB 内にあるか
    # ----------------------------------------------------------------
    def contains(self, pt: np.ndarray) -> bool:
        return bool(np.all(pt >= self.min_pt) and np.all(pt <= self.max_pt))

    # ----------------------------------------------------------------
    # QuickDraw Ch4: InsetRect 相当
    # AABB を均等に縮小
    # ----------------------------------------------------------------
    def inset(self, amount: float) -> "AABB":
        a = np.float32(amount)
        return AABB(
            min_pt=self.min_pt + a,
            max_pt=self.max_pt - a,
        )

    @property
    def center(self) -> np.ndarray:
        return (self.min_pt + self.max_pt) * 0.5

    @property
    def size(self) -> np.ndarray:
        return self.max_pt - self.min_pt

    @property
    def volume(self) -> float:
        s = self.size
        return float(s[0] * s[1] * s[2])

    @classmethod
    def from_points(cls, points: np.ndarray) -> "AABB":
        """点群から包含 AABB を生成"""
        return cls(
            min_pt=points.min(axis=0).astype(np.float32),
            max_pt=points.max(axis=0).astype(np.float32),
        )

    @classmethod
    def infinite(cls) -> "AABB":
        """無限大 AABB（全てと交差）"""
        inf = np.float32(1e30)
        return cls(min_pt=np.array([-inf, -inf, -inf]), max_pt=np.array([inf, inf, inf]))


# ----------------------------------------------------------------
# Frustum - 視錐台（6面のカリング平面）
# QuickDraw Ch3: SetClip の3D版
# ----------------------------------------------------------------
@dataclass
class Frustum:
    """
    透視投影カメラの視錐台。
    AABB との交差テストでボリュームカリングを行う。
    """
    planes: np.ndarray   # shape (6, 4) - ax+by+cz+d=0 の法線+距離

    @classmethod
    def from_camera(cls,
                    position:  np.ndarray,
                    direction: np.ndarray,
                    up:        np.ndarray,
                    fov_deg:   float,
                    aspect:    float,
                    near:      float,
                    far:       float) -> "Frustum":
        """カメラパラメーターから視錐台を生成"""
        import math

        fov  = math.radians(fov_deg)
        hfar = far  * math.tan(fov * 0.5)
        wfar = hfar * aspect

        d   = direction / np.linalg.norm(direction)
        r   = np.cross(d, up)
        r  /= np.linalg.norm(r)
        u   = np.cross(r, d)

        fc  = position + d * far
        nc  = position + d * near

        # 6面の法線・点を計算
        planes = []
        # Near / Far
        planes.append(cls._plane_from_normal_pt( d, nc))
        planes.append(cls._plane_from_normal_pt(-d, fc))
        # Top / Bottom
        top_n = np.cross(r, fc + u * hfar - position)
        top_n /= np.linalg.norm(top_n)
        planes.append(cls._plane_from_normal_pt( top_n, position))
        planes.append(cls._plane_from_normal_pt(-top_n, position))
        # Right / Left
        right_n = np.cross(fc + r * wfar - position, u)
        right_n /= np.linalg.norm(right_n)
        planes.append(cls._plane_from_normal_pt( right_n, position))
        planes.append(cls._plane_from_normal_pt(-right_n, position))

        return cls(planes=np.array(planes, dtype=np.float32))

    @staticmethod
    def _plane_from_normal_pt(n: np.ndarray, pt: np.ndarray) -> List[float]:
        nn = n / np.linalg.norm(n)
        d  = -float(np.dot(nn, pt))
        return [float(nn[0]), float(nn[1]), float(nn[2]), d]

    def intersects_aabb(self, aabb: AABB) -> bool:
        """
        AABB が視錐台と交差するか判定。
        全6面に対して「AABB の最も遠い頂点が面の外側にある」かチェック。
        QuickDraw Ch3: RegionClipping の3D版。
        """
        for plane in self.planes:
            nx, ny, nz, d = plane
            # 法線方向に最も遠い AABB の頂点（positive vertex）
            px = aabb.max_pt[0] if nx >= 0 else aabb.min_pt[0]
            py = aabb.max_pt[1] if ny >= 0 else aabb.min_pt[1]
            pz = aabb.max_pt[2] if nz >= 0 else aabb.min_pt[2]
            if nx * px + ny * py + nz * pz + d < 0:
                return False   # 完全に視錐台の外側
        return True

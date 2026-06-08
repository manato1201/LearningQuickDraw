"""
bvh.py
Bounding Volume Hierarchy (BVH) ツリー

Phase 1/2 の BVHCuller を汎用化。
VDB・Alembic・点群など任意のジオメトリに適用できる。

QuickDraw Ch3: RegionClipping の3D階層版
- 視野外ノードをまるごとスキップ
- Phase 1 fillTriangleZ の「描かないことが最大の最適化」思想
"""

import numpy as np
from dataclasses import dataclass, field
from typing import List, Optional, Any
from .aabb import AABB, Frustum


@dataclass
class BVHNode:
    aabb:     AABB
    children: List["BVHNode"] = field(default_factory=list)
    items:    List[Any]       = field(default_factory=list)   # 葉ノードのアイテム

    @property
    def is_leaf(self) -> bool:
        return len(self.children) == 0


class BVHTree:
    """
    SAH (Surface Area Heuristic) ベースの BVH 構築。
    任意の「AABB を持つアイテム」のリストから構築できる汎用実装。
    """

    def __init__(self, max_leaf_items: int = 4):
        self.max_leaf_items = max_leaf_items
        self.root: Optional[BVHNode] = None
        self.build_count = 0   # 再構築回数（統計）

    def build(self, items: List[Any], get_aabb) -> None:
        """
        items: 任意オブジェクトのリスト
        get_aabb: items[i] から AABB を返す関数
        """
        if not items:
            self.root = None
            return

        aabbs = [get_aabb(item) for item in items]
        self.root = self._build_node(list(range(len(items))), items, aabbs)
        self.build_count += 1

    def _build_node(self, indices, items, aabbs) -> BVHNode:
        # 現在ノードの AABB = 全アイテムの合成
        node_aabb = aabbs[indices[0]]
        for i in indices[1:]:
            node_aabb = node_aabb.union(aabbs[i])

        node = BVHNode(aabb=node_aabb)

        if len(indices) <= self.max_leaf_items:
            # 葉ノード
            node.items = [items[i] for i in indices]
            return node

        # SAH で最適な分割軸・位置を求める
        axis, split_pos = self._find_best_split(indices, aabbs, node_aabb)

        left_idx  = []
        right_idx = []
        for i in indices:
            c = aabbs[i].center[axis]
            if c <= split_pos:
                left_idx.append(i)
            else:
                right_idx.append(i)

        # 分割できない場合は強制的に半分ずつ
        if not left_idx or not right_idx:
            mid = len(indices) // 2
            left_idx  = indices[:mid]
            right_idx = indices[mid:]

        node.children.append(self._build_node(left_idx,  items, aabbs))
        node.children.append(self._build_node(right_idx, items, aabbs))
        return node

    def _find_best_split(self, indices, aabbs, node_aabb):
        """
        SAH: 各軸のコストを比較して最も効率的な分割を選ぶ。
        コスト = 左子面積 * 左子アイテム数 + 右子面積 * 右子アイテム数
        """
        best_cost  = float("inf")
        best_axis  = 0
        best_split = node_aabb.center[0]

        node_sa = self._surface_area(node_aabb)

        for axis in range(3):
            # 重心でソートして候補分割点を生成
            sorted_idx = sorted(indices, key=lambda i: aabbs[i].center[axis])
            n = len(sorted_idx)

            # 左からの累積AABB
            left_aabb  = [None] * n
            right_aabb = [None] * n
            la = aabbs[sorted_idx[0]]
            for k in range(n):
                la = la.union(aabbs[sorted_idx[k]])
                left_aabb[k] = la

            ra = aabbs[sorted_idx[-1]]
            for k in range(n - 1, -1, -1):
                ra = ra.union(aabbs[sorted_idx[k]])
                right_aabb[k] = ra

            # 各分割点でコスト計算
            for k in range(1, n):
                left_n  = k
                right_n = n - k
                cost = (self._surface_area(left_aabb[k-1])  * left_n +
                        self._surface_area(right_aabb[k])   * right_n) / node_sa
                if cost < best_cost:
                    best_cost  = cost
                    best_axis  = axis
                    best_split = (aabbs[sorted_idx[k-1]].center[axis] +
                                  aabbs[sorted_idx[k]].center[axis]) * 0.5

        return best_axis, best_split

    @staticmethod
    def _surface_area(aabb: AABB) -> float:
        s = aabb.size
        return 2.0 * (s[0]*s[1] + s[1]*s[2] + s[2]*s[0])

    # ----------------------------------------------------------------
    # クエリ: 視錐台カリング
    # QuickDraw Ch3: SetClip + RegionClipping の3D版
    # ----------------------------------------------------------------
    def query_frustum(self, frustum: Frustum) -> List[Any]:
        """視錐台内のアイテムを収集"""
        result = []
        if self.root:
            self._traverse_frustum(self.root, frustum, result)
        return result

    def _traverse_frustum(self, node: BVHNode, frustum: Frustum, result: List):
        if not frustum.intersects_aabb(node.aabb):
            return   # このノード以下をスキップ（カリング）
        if node.is_leaf:
            result.extend(node.items)
            return
        for child in node.children:
            self._traverse_frustum(child, frustum, result)

    # ----------------------------------------------------------------
    # クエリ: AABB 交差
    # ----------------------------------------------------------------
    def query_aabb(self, query: AABB) -> List[Any]:
        result = []
        if self.root:
            self._traverse_aabb(self.root, query, result)
        return result

    def _traverse_aabb(self, node: BVHNode, query: AABB, result: List):
        if not node.aabb.intersects(query):
            return
        if node.is_leaf:
            result.extend(node.items)
            return
        for child in node.children:
            self._traverse_aabb(child, query, result)

    # ----------------------------------------------------------------
    # 統計情報
    # ----------------------------------------------------------------
    def stats(self) -> dict:
        if not self.root:
            return {"nodes": 0, "leaves": 0, "depth": 0, "items": 0}
        nodes = leaves = items = 0
        max_depth = [0]

        def walk(n, depth):
            nonlocal nodes, leaves, items
            nodes += 1
            max_depth[0] = max(max_depth[0], depth)
            if n.is_leaf:
                leaves += 1
                items  += len(n.items)
            for c in n.children:
                walk(c, depth + 1)

        walk(self.root, 0)
        return {"nodes": nodes, "leaves": leaves,
                "depth": max_depth[0], "items": items}

"""
render-optimizer
汎用描画最適化ライブラリ

対応フォーマット: Jet .vol / OpenVDB / Alembic
最適化手法: BVH カリング / LOD / ダーティレクト / LRU キャッシュ
"""

from .aabb          import AABB, Frustum
from .bvh           import BVHTree, BVHNode
from .lod           import LODSelector, VoxelLOD, MeshLOD, DEFAULT_LOD_SCHEDULE
from .optimizer     import RenderOptimizer, OptimizeResult, OptimizeStats
from .cache_formats import VDBAdapter, AlembicAdapter, load_cache

__version__ = "1.0.0"
__all__ = [
    "AABB", "Frustum",
    "BVHTree", "BVHNode",
    "LODSelector", "VoxelLOD", "MeshLOD",
    "RenderOptimizer", "OptimizeResult", "OptimizeStats",
    "VDBAdapter", "AlembicAdapter", "load_cache",
]

"""
test_optimizer.py
RenderOptimizer の動作確認テスト

実行方法:
  cd C:/Users/matuu/Desktop/GameDevelopment/Learners/render-optimizer
  python -m pytest tests/ -v
  または
  python tests/test_optimizer.py
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import numpy as np
from src.aabb      import AABB, Frustum
from src.bvh       import BVHTree
from src.lod       import LODSelector, VoxelLOD
from src.optimizer import RenderOptimizer


# ================================================================
# ダミーアイテム（テスト用）
# ================================================================
class DummyItem:
    def __init__(self, name: str, aabb: AABB):
        self.name = name
        self.aabb = aabb

    def __repr__(self):
        return f"Item({self.name})"


# ================================================================
# テスト
# ================================================================
def test_aabb_operations():
    print("\n--- AABB Operations ---")
    a = AABB(min_pt=np.array([0,0,0], np.float32),
             max_pt=np.array([2,2,2], np.float32))
    b = AABB(min_pt=np.array([1,1,1], np.float32),
             max_pt=np.array([3,3,3], np.float32))
    c = AABB(min_pt=np.array([5,5,5], np.float32),
             max_pt=np.array([7,7,7], np.float32))

    assert a.intersects(b), "A と B は交差するはず"
    assert not a.intersects(c), "A と C は交差しないはず"

    union_ab = a.union(b)
    assert np.allclose(union_ab.min_pt, [0,0,0])
    assert np.allclose(union_ab.max_pt, [3,3,3])

    assert a.contains(np.array([1,1,1], np.float32))
    assert not a.contains(np.array([3,3,3], np.float32))

    print("  intersects: OK")
    print("  union:      OK")
    print("  contains:   OK")


def test_bvh_build_and_query():
    print("\n--- BVH Build & Query ---")

    items = []
    for x in range(5):
        for y in range(5):
            for z in range(5):
                aabb = AABB(
                    min_pt=np.array([x*2.0, y*2.0, z*2.0], np.float32),
                    max_pt=np.array([x*2.0+1, y*2.0+1, z*2.0+1], np.float32),
                )
                items.append(DummyItem(f"({x},{y},{z})", aabb))

    bvh = BVHTree(max_leaf_items=4)
    bvh.build(items, lambda item: item.aabb)

    stats = bvh.stats()
    print(f"  Items: {stats['items']}, Nodes: {stats['nodes']}, "
          f"Leaves: {stats['leaves']}, Depth: {stats['depth']}")
    assert stats["items"] == 125

    # 小さな AABB クエリ → 一部だけヒット
    query = AABB(min_pt=np.array([0,0,0], np.float32),
                 max_pt=np.array([3,3,3], np.float32))
    result = bvh.query_aabb(query)
    print(f"  Query AABB [0-3]^3: {len(result)} items hit (expected ~8)")
    assert 0 < len(result) <= 27, f"予期しない数: {len(result)}"
    print("  BVH build & query: OK")


def test_lod_selection():
    print("\n--- LOD Selection ---")

    selector = LODSelector()
    aabb = AABB(min_pt=np.array([-1,-1,-1], np.float32),
                max_pt=np.array([ 1, 1, 1], np.float32))

    cam_near = np.array([0, 0, 3], np.float32)   # 距離 2
    cam_far  = np.array([0, 0, 50], np.float32)  # 距離 49

    lod_near = selector.select(cam_near, aabb)
    lod_far  = selector.select(cam_far,  aabb)

    print(f"  Distance ~2  -> LOD {lod_near}")
    print(f"  Distance ~49 -> LOD {lod_far}")
    assert lod_near <= lod_far, "近い方が高品質（LOD値が低い）はず"
    print("  LOD selection: OK")


def test_voxel_lod_downsample():
    print("\n--- Voxel LOD Downsample ---")

    data = np.random.rand(64, 64, 64).astype(np.float32)

    d1 = VoxelLOD.downsample(data, 1)
    d2 = VoxelLOD.downsample(data, 2)

    print(f"  LOD0: {data.shape} ({data.size} voxels)")
    print(f"  LOD1: {d1.shape} ({d1.size} voxels) "
          f"ratio={d1.size/data.size:.4f} (expected 0.1250)")
    print(f"  LOD2: {d2.shape} ({d2.size} voxels) "
          f"ratio={d2.size/data.size:.4f} (expected 0.0156)")

    assert d1.shape == (32, 32, 32)
    assert d2.shape == (16, 16, 16)
    print("  Voxel downsample: OK")


def test_dirty_rect():
    print("\n--- Dirty Rect Detection ---")

    opt = RenderOptimizer()

    data1 = np.zeros((32, 32, 32), dtype=np.float32)
    data2 = data1.copy()
    data2[10:20, 10:20, 10:20] = 1.0   # 一部だけ変化

    cam   = np.array([0, 0, 10], np.float32)
    d     = np.array([0, 0,-1], np.float32)
    up    = np.array([0, 1, 0], np.float32)

    # 1フレーム目（全変化）
    r1 = opt.optimize(cam, d, up, 60.0, 1.333, 0.1, 100.0,
                      volume_data=data1)
    print(f"  Frame1 skip: {r1.stats.skip_ratio*100:.1f}% (expected 0%)")
    assert r1.stats.skip_ratio == 0.0

    # 2フレーム目（一部変化）
    r2 = opt.optimize(cam, d, up, 60.0, 1.333, 0.1, 100.0,
                      volume_data=data2)
    changed = 10**3
    total   = 32**3
    expected_skip = 1.0 - changed / total
    print(f"  Frame2 skip: {r2.stats.skip_ratio*100:.1f}% "
          f"(expected ~{expected_skip*100:.1f}%)")
    assert r2.stats.skip_ratio > 0.8, "8割以上スキップされるはず"
    print("  Dirty rect: OK")


def test_full_pipeline():
    print("\n--- Full Pipeline ---")

    opt = RenderOptimizer()

    # 100個のアイテムを5x5x4グリッドに配置
    items = []
    for x in range(5):
        for y in range(5):
            for z in range(4):
                aabb = AABB(
                    min_pt=np.array([x*3.0-7, y*3.0-7, z*3.0-6], np.float32),
                    max_pt=np.array([x*3.0-5, y*3.0-5, z*3.0-4], np.float32),
                )
                items.append(DummyItem(f"block_{x}_{y}_{z}", aabb))

    opt.build_bvh(items, lambda item: item.aabb)

    # カメラを正面に向けた場合
    cam = np.array([0, 0, 20], np.float32)
    d   = np.array([0, 0, -1], np.float32)
    up  = np.array([0, 1, 0], np.float32)

    vol_data = np.random.rand(32,32,32).astype(np.float32)

    result = opt.optimize(cam, d, up, 60.0, 1.333, 0.1, 50.0,
                          volume_data=vol_data)

    print(f"  {result.stats.summary()}")
    print(f"  BVH stats: {opt.bvh_stats()}")
    print("  Full pipeline: OK")


if __name__ == "__main__":
    test_aabb_operations()
    test_bvh_build_and_query()
    test_lod_selection()
    test_voxel_lod_downsample()
    test_dirty_rect()
    test_full_pipeline()
    print("\nAll tests passed!")

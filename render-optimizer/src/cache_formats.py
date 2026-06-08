"""
cache_formats.py
VDB / Alembic キャッシュ読み込みアダプター

外部ライブラリ（pyopenvdb, alembic）が利用可能な場合はそちらを使用。
ない場合は軽量フォールバックを使用する。
"""

import os
import numpy as np
from dataclasses import dataclass
from typing import Optional, List
from .aabb import AABB


# ================================================================
# 共通データ型
# ================================================================
@dataclass
class VolumeData:
    """ボリュームキャッシュの共通表現"""
    data:      np.ndarray    # (xRes, yRes, zRes) float32
    aabb:      AABB
    frame:     int
    source:    str           # "vol" / "vdb" / "abc"


@dataclass
class MeshData:
    """メッシュキャッシュの共通表現"""
    vertices:  np.ndarray    # (N, 3) float32
    indices:   np.ndarray    # (M, 3) int32
    aabb:      AABB
    frame:     int
    source:    str


# ================================================================
# VDB アダプター
# ================================================================
class VDBAdapter:
    """
    OpenVDB (.vdb) ファイルの読み込み。
    pyopenvdb が利用可能な場合は使用、なければエラーを返す。
    """

    @staticmethod
    def available() -> bool:
        try:
            import pyopenvdb
            return True
        except ImportError:
            return False

    @staticmethod
    def read(path: str, grid_name: str = "density",
             frame: int = 0) -> Optional[VolumeData]:
        try:
            import pyopenvdb as vdb
        except ImportError:
            print("[VDBAdapter] pyopenvdb not available.")
            return None

        grids = vdb.read(path)
        target = None
        for g in grids:
            if g.name == grid_name:
                target = g
                break

        if target is None:
            print(f"[VDBAdapter] Grid '{grid_name}' not found in {path}")
            return None

        # VDB のアクティブボクセルを Dense 配列に変換
        bbox       = target.evalActiveVoxelBoundingBox()
        min_idx    = np.array(bbox[0], dtype=np.int32)
        max_idx    = np.array(bbox[1], dtype=np.int32)
        shape      = max_idx - min_idx + 1

        data = np.zeros(shape, dtype=np.float32)
        accessor = target.getAccessor()
        for ix in range(shape[0]):
            for iy in range(shape[1]):
                for iz in range(shape[2]):
                    v = accessor.getValue(
                        (int(min_idx[0]+ix),
                         int(min_idx[1]+iy),
                         int(min_idx[2]+iz))
                    )
                    data[ix, iy, iz] = v

        # ワールド空間 AABB
        world_min = target.transform.indexToWorld(bbox[0])
        world_max = target.transform.indexToWorld(bbox[1])
        aabb = AABB(
            min_pt=np.array(world_min, dtype=np.float32),
            max_pt=np.array(world_max, dtype=np.float32),
        )

        return VolumeData(data=data, aabb=aabb, frame=frame, source="vdb")


# ================================================================
# Alembic アダプター
# ================================================================
class AlembicAdapter:
    """
    Alembic (.abc) メッシュキャッシュの読み込み。
    alembic Python バインディングが必要。
    """

    @staticmethod
    def available() -> bool:
        try:
            import alembic
            return True
        except ImportError:
            return False

    @staticmethod
    def read(path: str, object_path: str = "/",
             frame: int = 0, fps: float = 24.0) -> Optional[MeshData]:
        try:
            from alembic import Abc, AbcGeom
        except ImportError:
            print("[AlembicAdapter] alembic not available.")
            return None

        archive = Abc.IArchive(path)
        time    = frame / fps

        # オブジェクトパス解決
        obj = archive.getTop()
        for part in object_path.strip("/").split("/"):
            if not part:
                continue
            if obj.getChild(part).valid():
                obj = obj.getChild(part)
            else:
                print(f"[AlembicAdapter] Path not found: {object_path}")
                return None

        # PolyMesh を取得
        mesh_obj = AbcGeom.IPolyMesh(obj, AbcGeom.kWrapExisting)
        if not mesh_obj.valid():
            print("[AlembicAdapter] Not a PolyMesh")
            return None

        schema = mesh_obj.getSchema()
        sample = schema.getValue(Abc.ISampleSelector(time))

        verts_flat = np.array(sample.getPositions(), dtype=np.float32)
        verts      = verts_flat.reshape(-1, 3)

        counts  = np.array(sample.getFaceCounts(), dtype=np.int32)
        indices = np.array(sample.getFaceIndices(), dtype=np.int32)

        # 三角形化（4角形以上も対応）
        triangles = AlembicAdapter._triangulate(indices, counts)

        aabb = AABB.from_points(verts)
        return MeshData(vertices=verts, indices=triangles,
                        aabb=aabb, frame=frame, source="abc")

    @staticmethod
    def _triangulate(indices: np.ndarray,
                     counts: np.ndarray) -> np.ndarray:
        """ポリゴン配列を三角形に分割（Fan triangulation）"""
        tris = []
        ptr  = 0
        for count in counts:
            face = indices[ptr:ptr+count]
            for i in range(1, count - 1):
                tris.append([face[0], face[i], face[i+1]])
            ptr += count
        return np.array(tris, dtype=np.int32) if tris else np.zeros((0,3), np.int32)


# ================================================================
# フォーマット自動判別
# ================================================================
def load_cache(path: str, frame: int = 0, **kwargs) -> Optional[object]:
    """
    ファイル拡張子からアダプターを自動選択して読み込む。
    """
    ext = os.path.splitext(path)[1].lower()
    if ext == ".vol":
        from .vol_reader import read_vol
        return read_vol(path)
    elif ext == ".vdb":
        return VDBAdapter.read(path, frame=frame,
                               grid_name=kwargs.get("grid_name", "density"))
    elif ext == ".abc":
        return AlembicAdapter.read(path, frame=frame,
                                   object_path=kwargs.get("object_path", "/"),
                                   fps=kwargs.get("fps", 24.0))
    else:
        print(f"[load_cache] Unknown format: {ext}")
        return None

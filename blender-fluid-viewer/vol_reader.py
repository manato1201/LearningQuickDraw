"""
vol_reader.py
Jet / Mitsuba .vol volume cache reader

.vol binary format:
  [0:3]   magic: b"VOL"
  [3]     version: 3
  [4:8]   encoding: int32 (1 = float32)
  [8:12]  xRes: int32
  [12:16] yRes: int32
  [16:20] zRes: int32
  [20:24] nChannels: int32
  [24:48] AABB: 6x float32 (minX,minY,minZ, maxX,maxY,maxZ)
  [48:]   data: float32[xRes * yRes * zRes * nChannels]
"""

import struct
import numpy as np
from dataclasses import dataclass
from typing import Optional


@dataclass
class VolHeader:
    x_res:      int
    y_res:      int
    z_res:      int
    n_channels: int
    aabb_min:   tuple   # (x, y, z)
    aabb_max:   tuple   # (x, y, z)


@dataclass
class VolFrame:
    header: VolHeader
    data:   np.ndarray   # shape: (xRes, yRes, zRes) float32


def read_vol(path: str) -> Optional[VolFrame]:
    """
    .vol ファイルを読み込み VolFrame を返す。
    読み込み失敗時は None を返す。
    """
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError:
        print(f"[vol_reader] File not found: {path}")
        return None

    if len(raw) < 48:
        print(f"[vol_reader] File too short: {path}")
        return None

    # Magic check
    magic = raw[0:3]
    if magic != b"VOL":
        print(f"[vol_reader] Invalid magic: {magic}")
        return None

    # Header parse
    version    = raw[3]
    encoding   = struct.unpack_from("<i", raw, 4)[0]
    x_res      = struct.unpack_from("<i", raw, 8)[0]
    y_res      = struct.unpack_from("<i", raw, 12)[0]
    z_res      = struct.unpack_from("<i", raw, 16)[0]
    n_channels = struct.unpack_from("<i", raw, 20)[0]
    aabb       = struct.unpack_from("<6f",raw, 24)

    expected = 48 + x_res * y_res * z_res * n_channels * 4
    if len(raw) < expected:
        print(f"[vol_reader] Data truncated: expected {expected}, got {len(raw)}")
        return None

    # Data: float32 array
    count  = x_res * y_res * z_res * n_channels
    data_f = np.frombuffer(raw, dtype=np.float32, count=count, offset=48)
    data_f = data_f.reshape((x_res, y_res, z_res))

    header = VolHeader(
        x_res      = x_res,
        y_res      = y_res,
        z_res      = z_res,
        n_channels = n_channels,
        aabb_min   = (aabb[0], aabb[1], aabb[2]),
        aabb_max   = (aabb[3], aabb[4], aabb[5]),
    )
    return VolFrame(header=header, data=data_f)


def vol_frame_path(cache_dir: str, frame: int) -> str:
    """
    キャッシュディレクトリとフレーム番号からファイルパスを生成。
    smoke_sim の出力規則: smoke_{frame:04d}.vol
    """
    import os
    return os.path.join(cache_dir, f"smoke_{frame:04d}.vol")

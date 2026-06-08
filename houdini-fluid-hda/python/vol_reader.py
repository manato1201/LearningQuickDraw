"""
vol_reader.py
Jet / Mitsuba .vol reader (Houdini 共通ユーティリティ)
Blender 版と同一実装 - 両環境で再利用可能
"""

import struct
import os
import numpy as np
from dataclasses import dataclass
from typing import Optional


@dataclass
class VolHeader:
    x_res:      int
    y_res:      int
    z_res:      int
    n_channels: int
    aabb_min:   tuple
    aabb_max:   tuple


@dataclass
class VolFrame:
    header: VolHeader
    data:   np.ndarray   # (xRes, yRes, zRes) float32


def read_vol(path: str) -> Optional[VolFrame]:
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError:
        return None

    if len(raw) < 48 or raw[0:3] != b"VOL":
        return None

    x_res      = struct.unpack_from("<i", raw,  8)[0]
    y_res      = struct.unpack_from("<i", raw, 12)[0]
    z_res      = struct.unpack_from("<i", raw, 16)[0]
    n_channels = struct.unpack_from("<i", raw, 20)[0]
    aabb       = struct.unpack_from("<6f", raw, 24)

    count = x_res * y_res * z_res * n_channels
    if len(raw) < 48 + count * 4:
        return None

    data = np.frombuffer(raw, dtype=np.float32, count=count, offset=48)
    data = data.reshape((x_res, y_res, z_res))

    return VolFrame(
        header=VolHeader(x_res, y_res, z_res, n_channels,
                         aabb[:3], aabb[3:]),
        data=data,
    )


def vol_frame_path(cache_dir: str, frame: int) -> str:
    return os.path.join(cache_dir, f"smoke_{frame:04d}.vol")


def downsample(vol: VolFrame, lod: int) -> VolFrame:
    if lod <= 0:
        return vol
    step = 2 ** lod
    d    = vol.data[::step, ::step, ::step].copy()
    h    = vol.header
    return VolFrame(
        header=VolHeader(d.shape[0], d.shape[1], d.shape[2],
                         h.n_channels, h.aabb_min, h.aabb_max),
        data=d,
    )

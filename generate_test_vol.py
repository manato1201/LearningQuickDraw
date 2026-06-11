"""
generate_test_vol.py
テスト用 .vol キャッシュファイルを生成する。
煙が上昇するシンプルなシミュレーションを模倣。
"""

import struct
import numpy as np
import os

OUTPUT_DIR = "./test_vol_cache"
NUM_FRAMES = 30
RES = 32  # 32x32x32 voxels

os.makedirs(OUTPUT_DIR, exist_ok=True)

for frame in range(NUM_FRAMES):
    xr, yr, zr = RES, RES, RES
    data = np.zeros((xr, yr, zr), dtype=np.float32)

    # 煙が下から上に上昇するシミュレーション
    rise = frame / NUM_FRAMES  # 0.0 -> 1.0
    center_y = int(rise * (yr - 8)) + 4

    for y in range(yr):
        dist_y = abs(y - center_y)
        if dist_y > 10:
            continue
        for x in range(xr):
            for z in range(zr):
                cx, cz = xr // 2, zr // 2
                r = np.sqrt((x - cx)**2 + (z - cz)**2 + (y - center_y)**2)
                # ガウシアン密度分布
                density = np.exp(-r**2 / (2 * (4 + frame * 0.3)**2))
                data[x, y, z] = float(density)

    # .vol バイナリ書き出し
    path = os.path.join(OUTPUT_DIR, f"smoke_{frame:04d}.vol")
    with open(path, "wb") as f:
        f.write(b"VOL")           # magic
        f.write(bytes([3]))       # version
        f.write(struct.pack("<i", 1))   # encoding (float32)
        f.write(struct.pack("<i", xr))
        f.write(struct.pack("<i", yr))
        f.write(struct.pack("<i", zr))
        f.write(struct.pack("<i", 1))   # nChannels
        # AABB: -1,-1,-1 -> 1,1,1
        f.write(struct.pack("<6f", -1.0, -1.0, -1.0, 1.0, 1.0, 1.0))
        f.write(data.astype(np.float32).tobytes())

    print(f"Frame {frame:03d}: {path}  max_density={data.max():.3f}")

print(f"\nDone: {NUM_FRAMES} frames -> {OUTPUT_DIR}/")

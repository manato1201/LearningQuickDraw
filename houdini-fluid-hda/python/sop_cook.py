"""
sop_cook.py
Houdini Python SOP の Cook スクリプト。

HDA の「Script」タブの「Cook Code」欄にこの内容を貼り付ける。
または HDA 内部の PythonSOP として使用する。

出力ジオメトリ:
  - Points: 密度ボクセルの座標 + 密度アトリビュート
  - Volume: Houdini Volume プリミティブ（VDB 変換可能）

パラメーター:
  cache_dir   STR  キャッシュディレクトリ
  lod         INT  LOD レベル (0=Full, 1=Half, 2=Quarter)
  threshold   FLT  ダーティレクト閾値
  output_mode INT  0=Points, 1=Volume
  density_min FLT  表示する最低密度
"""

import hou
import sys
import os
import numpy as np
import time

# ----------------------------------------------------------------
# Python パス追加（HDA 内の python フォルダを参照）
# ----------------------------------------------------------------
_hda_dir = os.path.dirname(os.path.abspath(__file__))
if _hda_dir not in sys.path:
    sys.path.insert(0, _hda_dir)

from vol_reader  import read_vol, vol_frame_path, downsample
from dirty_tracker import DirtyTracker

# ----------------------------------------------------------------
# ノードごとのステート管理（Cook をまたいで状態を保持）
# ----------------------------------------------------------------
_node_state = {}   # key: node.path() -> dict


def _get_state(node) -> dict:
    key = node.path()
    if key not in _node_state:
        _node_state[key] = {
            "tracker": DirtyTracker(),
            "prev_frame": -9999,
        }
    return _node_state[key]


# ----------------------------------------------------------------
# メイン Cook 関数
# ----------------------------------------------------------------
def cook(node, geo):
    """
    Houdini Python SOP の cook エントリポイント。
    node: hou.SopNode
    geo:  hou.Geometry (出力先)
    """
    t0 = time.perf_counter()

    # パラメーター取得
    cache_dir   = node.parm("cache_dir").eval()
    lod         = node.parm("lod").eval()
    threshold   = node.parm("threshold").eval()
    output_mode = node.parm("output_mode").eval()
    density_min = node.parm("density_min").eval()
    frame_idx   = int(hou.frame())

    if not cache_dir or not os.path.isdir(cache_dir):
        node.setWarningMessage("Cache directory not set or not found.")
        return

    # .vol 読み込み
    path = vol_frame_path(cache_dir, frame_idx)
    vol  = read_vol(path)
    if vol is None:
        node.setWarningMessage(f"Frame {frame_idx}: {path} not found.")
        return

    # LOD ダウンサンプリング
    if lod > 0:
        vol = downsample(vol, lod)

    # ダーティレクト計算
    state   = _get_state(node)
    tracker = state["tracker"]
    tracker.threshold = threshold

    # フレームが戻った場合はリセット（逆再生対応）
    if frame_idx < state["prev_frame"]:
        tracker.reset()
    state["prev_frame"] = frame_idx

    dirty_mask = tracker.update(vol)

    # 出力ジオメトリ生成
    if output_mode == 0:
        _output_points(geo, vol, dirty_mask, density_min)
    else:
        _output_volume(geo, vol, density_min)

    elapsed = (time.perf_counter() - t0) * 1000.0

    # ノードのコメントに統計情報を表示
    skip_pct = tracker.skip_ratio * 100.0
    node.setComment(
        f"Frame: {frame_idx}  LOD: {lod}\n"
        f"Cook: {elapsed:.1f} ms\n"
        f"Skip: {skip_pct:.1f}%  "
        f"Changed: {tracker.changed_voxels}/{tracker.total_voxels}"
    )


# ----------------------------------------------------------------
# Points 出力
# ----------------------------------------------------------------
def _output_points(geo, vol, dirty_mask, density_min):
    """
    密度ボクセルを Houdini Points として出力。
    ダーティレクト: 変化ボクセルのみ処理。
    QuickDraw Ch3: RegionClipping の3D版。
    """
    h     = vol.header
    data  = vol.data

    # 有効ボクセル = 変化あり かつ 密度 > density_min
    active = dirty_mask & (data > density_min)
    coords = np.argwhere(active)   # shape (N, 3)

    if coords.size == 0:
        return

    # ワールド座標変換
    step = np.array([
        (h.aabb_max[0] - h.aabb_min[0]) / h.x_res,
        (h.aabb_max[1] - h.aabb_min[1]) / h.y_res,
        (h.aabb_max[2] - h.aabb_min[2]) / h.z_res,
    ], dtype=np.float32)

    origin = np.array(h.aabb_min, dtype=np.float32)
    world  = origin + coords * step   # (N, 3)

    densities = data[coords[:, 0], coords[:, 1], coords[:, 2]]

    # Houdini ジオメトリに追加
    geo.createPoints(world.tolist())

    # density アトリビュートを追加
    attrib = geo.addAttrib(hou.attribType.Point, "density", 0.0)
    for i, pt in enumerate(geo.points()):
        pt.setAttribValue(attrib, float(densities[i]))


# ----------------------------------------------------------------
# Volume 出力
# ----------------------------------------------------------------
def _output_volume(geo, vol, density_min):
    """
    密度グリッドを Houdini Volume プリミティブとして出力。
    後から Convert VDB SOP で VDB に変換可能。
    """
    h    = vol.header
    data = vol.data

    aabb_min = hou.Vector3(h.aabb_min)
    aabb_max = hou.Vector3(h.aabb_max)
    center   = (aabb_min + aabb_max) * 0.5

    res = hou.Vector3(h.x_res, h.y_res, h.z_res)

    # Houdini Volume プリミティブ作成
    vol_prim = geo.createVolume(
        int(h.x_res), int(h.y_res), int(h.z_res),
        center,
        hou.Vector3(
            (h.aabb_max[0] - h.aabb_min[0]) * 0.5,
            (h.aabb_max[1] - h.aabb_min[1]) * 0.5,
            (h.aabb_max[2] - h.aabb_min[2]) * 0.5,
        )
    )

    # ボクセルデータを書き込み
    # density_min 未満は 0 に
    d = data.copy()
    d[d < density_min] = 0.0
    vol_prim.setAllVoxels(d.flatten().tolist())

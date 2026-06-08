"""
dirty_tracker.py
フレーム間差分追跡 (Blender 版と同一ロジック)
QuickDraw Ch3: RegionClipping の3Dボクセル版
"""

import numpy as np
from typing import Optional
from .vol_reader import VolFrame


class DirtyTracker:
    def __init__(self, threshold: float = 0.01):
        self.threshold           = threshold
        self._prev: Optional[np.ndarray] = None
        self.total_voxels        = 0
        self.changed_voxels      = 0
        self.skip_ratio          = 0.0

    def update(self, frame: VolFrame) -> np.ndarray:
        data = frame.data
        self.total_voxels = data.size

        if self._prev is None or self._prev.shape != data.shape:
            self._prev          = data.copy()
            self.changed_voxels = self.total_voxels
            self.skip_ratio     = 0.0
            return np.ones(data.shape, dtype=bool)

        mask                = np.abs(data - self._prev) > self.threshold
        self.changed_voxels = int(mask.sum())
        self.skip_ratio     = 1.0 - self.changed_voxels / self.total_voxels
        self._prev          = data.copy()
        return mask

    def reset(self):
        self._prev = None

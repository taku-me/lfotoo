"""Optical-flow motion-energy extraction (the offline, OpenCV half of the tool).

This is preprocessing on purpose: dense optical flow cannot run inside a real-time audio
callback, so the VST never touches video. It consumes the JSON this module helps produce.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Optional


@dataclass
class MotionResult:
    fps: float
    energy: List[float]  # one motion-energy value per frame (length == frame count)


def _require_cv2():
    try:
        import cv2  # noqa: F401
        import numpy  # noqa: F401
    except ImportError as exc:  # pragma: no cover - environment dependent
        raise ImportError(
            "motion extraction needs opencv-python and numpy: pip install opencv-python numpy"
        ) from exc
    return cv2, numpy


def extract_motion_energy(
    path: str,
    downscale: int = 4,
    max_frames: Optional[int] = None,
) -> MotionResult:
    """Compute per-frame motion energy with Farneback dense optical flow.

    downscale: integer factor to shrink frames before flow (speed; 4 = quarter resolution).
    max_frames: stop early after this many frames (useful for previews).

    energy[i] = mean magnitude of the flow field between frame i-1 and i (energy[0] == 0).
    """
    cv2, np = _require_cv2()

    cap = cv2.VideoCapture(path)
    if not cap.isOpened():
        raise IOError(f"could not open video: {path}")

    fps = cap.get(cv2.CAP_PROP_FPS) or 0.0
    if fps <= 0:
        fps = 30.0  # fall back to a sane default rather than dividing by zero downstream

    energy: List[float] = []
    prev_gray = None
    count = 0
    try:
        while True:
            if max_frames is not None and count >= max_frames:
                break
            ok, frame = cap.read()
            if not ok:
                break

            if downscale and downscale > 1:
                frame = cv2.resize(
                    frame,
                    (max(1, frame.shape[1] // downscale), max(1, frame.shape[0] // downscale)),
                    interpolation=cv2.INTER_AREA,
                )
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

            if prev_gray is None:
                energy.append(0.0)
            else:
                flow = cv2.calcOpticalFlowFarneback(
                    prev_gray, gray, None,
                    pyr_scale=0.5, levels=3, winsize=15,
                    iterations=3, poly_n=5, poly_sigma=1.2, flags=0,
                )
                mag = np.sqrt(flow[..., 0] ** 2 + flow[..., 1] ** 2)
                energy.append(float(mag.mean()))

            prev_gray = gray
            count += 1
    finally:
        cap.release()

    return MotionResult(fps=fps, energy=energy)

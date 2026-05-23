"""Hard-snap quantization of a continuous motion-energy curve to turntable RPM steps.

This module is the philosophical core of the tool: continuous values are *forbidden*
from passing through untouched. They are forced onto the discrete physical states of a
record player (33 1/3 / 45 rpm, with an optional 0 rpm "tape stop").

Pure Python on purpose: no numpy/OpenCV here so the algorithm stays testable in isolation.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Sequence

# Physical states of the turntable. These are the only values allowed out of this module.
RPM_STOP = 0.0
RPM_33 = 33.333
RPM_45 = 45.0

# Internal discrete levels (ordered low -> high).
_STOP, _LOW, _HIGH = 0, 1, 2
_LEVEL_TO_RPM = {_STOP: RPM_STOP, _LOW: RPM_33, _HIGH: RPM_45}


@dataclass
class QuantizeConfig:
    thr_low: float          # boundary between tape-stop and 33 rpm
    thr_high: float         # boundary between 33 rpm and 45 rpm
    enable_tapestop: bool = False
    hysteresis: float = 0.0  # Schmitt-trigger margin (same units as energy) to kill chatter
    min_hold: int = 1        # minimum committed run length, in steps

    def __post_init__(self) -> None:
        if self.thr_high < self.thr_low:
            raise ValueError("thr_high must be >= thr_low")
        if self.hysteresis < 0:
            raise ValueError("hysteresis must be >= 0")
        if self.min_hold < 1:
            raise ValueError("min_hold must be >= 1")


def _classify(energy: float, state: int, cfg: QuantizeConfig) -> int:
    """Schmitt-trigger classification: the threshold to cross depends on the current state."""
    h = cfg.hysteresis
    floor_level = _STOP if cfg.enable_tapestop else _LOW

    if state == _HIGH:
        if energy < cfg.thr_high - h:
            if cfg.enable_tapestop and energy < cfg.thr_low - h:
                return _STOP
            return _LOW
        return _HIGH

    if state == _LOW:
        if energy >= cfg.thr_high + h:
            return _HIGH
        if cfg.enable_tapestop and energy < cfg.thr_low - h:
            return _STOP
        return _LOW

    # state == _STOP
    if energy >= cfg.thr_high + h:
        return _HIGH
    if energy >= cfg.thr_low + h:
        return _LOW
    return floor_level


def _debounce(states: List[int], min_hold: int) -> List[int]:
    """Suppress committed runs shorter than ``min_hold`` by reverting them to the prior state.

    Guarantees the "click, click" cadence: no state survives for fewer than ``min_hold`` steps
    (except possibly the leading run, which has no predecessor to merge into).
    """
    if min_hold <= 1 or not states:
        return list(states)

    out = list(states)
    i = 1
    run_start = 0
    while i <= len(out):
        at_break = i == len(out) or out[i] != out[run_start]
        if at_break:
            run_len = i - run_start
            if run_len < min_hold and run_start > 0:
                prev = out[run_start - 1]
                for j in range(run_start, i):
                    out[j] = prev
                # Re-scan from the merged region in case it created a new short run boundary.
                run_start = run_start - 1
                while run_start > 0 and out[run_start - 1] == out[run_start]:
                    run_start -= 1
                i = run_start + 1
                continue
            run_start = i
        i += 1
    return out


def quantize(energy: Sequence[float], cfg: QuantizeConfig) -> List[float]:
    """Map a per-step energy sequence to a list of RPM values (one of RPM_STOP/33/45)."""
    state = _LOW if not cfg.enable_tapestop else _STOP
    raw: List[int] = []
    for e in energy:
        state = _classify(e, state, cfg)
        raw.append(state)
    held = _debounce(raw, cfg.min_hold)
    return [_LEVEL_TO_RPM[s] for s in held]


def downsample(
    energy: Sequence[float],
    src_fps: float,
    bpm: float,
    grid: int = 16,
    agg: str = "mean",
) -> List[float]:
    """Aggregate a per-video-frame energy curve onto a BPM grid.

    grid: note division (16 = 16th notes, 8 = 8th notes, ...). Steps per beat = grid / 4.
    Returns one aggregated energy value per grid step.
    """
    if src_fps <= 0 or bpm <= 0 or grid <= 0:
        raise ValueError("src_fps, bpm and grid must be > 0")
    if not energy:
        return []

    steps_per_sec = (bpm / 60.0) * (grid / 4.0)
    frames_per_step = src_fps / steps_per_sec
    if frames_per_step <= 0:
        raise ValueError("computed frames_per_step <= 0; check fps/bpm/grid")

    n_frames = len(energy)
    n_steps = max(1, int(round(n_frames / frames_per_step)))
    out: List[float] = []
    for s in range(n_steps):
        a = int(round(s * frames_per_step))
        b = int(round((s + 1) * frames_per_step))
        a = min(max(a, 0), n_frames)
        b = min(max(b, a + 1), n_frames)
        window = energy[a:b]
        if not window:
            window = [energy[min(a, n_frames - 1)]]
        if agg == "mean":
            out.append(sum(window) / len(window))
        elif agg in ("max", "peak"):
            out.append(max(window))
        else:
            raise ValueError(f"unknown agg: {agg!r} (use 'mean' or 'max')")
    return out


def percentile(values: Sequence[float], pct: float) -> float:
    """Linear-interpolated percentile (0..100). Dependency-free."""
    if not values:
        raise ValueError("empty values")
    if not 0 <= pct <= 100:
        raise ValueError("pct must be in [0, 100]")
    s = sorted(values)
    if len(s) == 1:
        return s[0]
    rank = (pct / 100.0) * (len(s) - 1)
    lo = int(rank)
    hi = min(lo + 1, len(s) - 1)
    frac = rank - lo
    return s[lo] * (1.0 - frac) + s[hi] * frac


def suggest_thresholds(energy: Sequence[float], low_pct: float = 50.0, high_pct: float = 80.0):
    """Suggest (thr_low, thr_high) from the energy distribution.

    Lets an agent pick thresholds without knowing absolute motion magnitudes: e.g. high_pct=80
    means only the most energetic ~20% of steps spin up to 45 rpm.
    """
    return percentile(energy, low_pct), percentile(energy, high_pct)

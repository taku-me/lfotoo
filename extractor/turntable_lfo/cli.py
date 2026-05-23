"""Command-line entry point: video -> turntable-RPM step-LFO (.json / .mid).

Examples
--------
    python -m turntable_lfo input.mp4 --bpm 120 --out lfo.json
    python -m turntable_lfo input.mp4 --bpm 174 --grid 16 --high-pct 80 --tape-stop \
        --format both --out groove

If --thr-low/--thr-high are omitted they are inferred from the motion distribution
(--low-pct/--high-pct), so you can drive the tool by intent ("only the busiest 20%
of the clip spins up to 45") without knowing absolute flow magnitudes.
"""

from __future__ import annotations

import argparse
import os
import sys
from typing import List

from . import export
from .motion import extract_motion_energy
from .quantize import QuantizeConfig, downsample, quantize, suggest_thresholds


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="turntable-lfo", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("video", help="input video file (.mp4 / .mov / ...)")
    p.add_argument("--bpm", type=float, required=True, help="project tempo for the LFO grid")
    p.add_argument("--grid", type=int, default=16, help="note division (16 = 16th notes)")
    p.add_argument("--agg", choices=["mean", "max"], default="mean",
                   help="how to aggregate frames into a grid step")

    p.add_argument("--thr-low", type=float, default=None, help="tape-stop / 33 boundary")
    p.add_argument("--thr-high", type=float, default=None, help="33 / 45 boundary")
    p.add_argument("--low-pct", type=float, default=50.0,
                   help="percentile for auto thr-low (when --thr-low omitted)")
    p.add_argument("--high-pct", type=float, default=80.0,
                   help="percentile for auto thr-high (when --thr-high omitted)")

    p.add_argument("--tape-stop", action="store_true", help="enable the 0 rpm (silence) state")
    p.add_argument("--hysteresis", type=float, default=0.0, help="Schmitt margin to kill chatter")
    p.add_argument("--min-hold", type=int, default=1, help="minimum steps a state must hold")

    p.add_argument("--downscale", type=int, default=4, help="shrink frames before optical flow")
    p.add_argument("--max-frames", type=int, default=None, help="stop after N frames (preview)")

    p.add_argument("--format", choices=["json", "mid", "both"], default="json")
    p.add_argument("--out", required=True, help="output path (extension added per --format)")
    return p


def _resolve_outputs(out: str, fmt: str) -> dict:
    base, ext = os.path.splitext(out)
    paths = {}
    if fmt in ("json", "both"):
        paths["json"] = out if ext.lower() == ".json" else base + ".json"
    if fmt in ("mid", "both"):
        paths["mid"] = out if ext.lower() in (".mid", ".midi") else base + ".mid"
    return paths


def run(argv: List[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    motion = extract_motion_energy(args.video, downscale=args.downscale, max_frames=args.max_frames)
    if not motion.energy:
        print("error: no frames decoded from video", file=sys.stderr)
        return 2

    stepped = downsample(motion.energy, motion.fps, args.bpm, args.grid, args.agg)

    thr_low, thr_high = args.thr_low, args.thr_high
    if thr_low is None or thr_high is None:
        auto_low, auto_high = suggest_thresholds(stepped, args.low_pct, args.high_pct)
        thr_low = auto_low if thr_low is None else thr_low
        thr_high = auto_high if thr_high is None else thr_high

    cfg = QuantizeConfig(
        thr_low=thr_low,
        thr_high=thr_high,
        enable_tapestop=args.tape_stop,
        hysteresis=args.hysteresis,
        min_hold=args.min_hold,
    )
    rpm = quantize(stepped, cfg)

    payload = export.to_dict(rpm, bpm=args.bpm, grid=args.grid, fps=motion.fps)
    outputs = _resolve_outputs(args.out, args.format)
    if "json" in outputs:
        export.write_json(outputs["json"], payload)
    if "mid" in outputs:
        export.write_midi(outputs["mid"], rpm, bpm=args.bpm, grid=args.grid)

    n45 = sum(1 for v in rpm if v >= 45.0)
    n33 = sum(1 for v in rpm if 33.0 <= v < 45.0)
    n0 = sum(1 for v in rpm if v < 33.0)
    print(f"fps={motion.fps:.3f} frames={len(motion.energy)} steps={len(rpm)} "
          f"thr=({thr_low:.4f},{thr_high:.4f}) | 45rpm={n45} 33rpm={n33} stop={n0}")
    for kind, path in outputs.items():
        print(f"wrote {kind}: {path}")
    return 0


def main() -> None:
    raise SystemExit(run())


if __name__ == "__main__":
    main()

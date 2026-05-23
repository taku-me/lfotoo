"""Tests for the dependency-free quantization core."""

import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from turntable_lfo import export  # noqa: E402
from turntable_lfo.quantize import (  # noqa: E402
    RPM_33,
    RPM_45,
    RPM_STOP,
    QuantizeConfig,
    _debounce,
    downsample,
    percentile,
    quantize,
    suggest_thresholds,
)


def test_two_state_hard_snap():
    cfg = QuantizeConfig(thr_low=0.0, thr_high=1.0)
    energy = [0.5, 1.5, 0.9, 2.0]
    assert quantize(energy, cfg) == [RPM_33, RPM_45, RPM_33, RPM_45]


def test_no_intermediate_values_ever():
    cfg = QuantizeConfig(thr_low=0.3, thr_high=0.7, enable_tapestop=True)
    energy = [i / 100.0 for i in range(100)]
    allowed = {RPM_STOP, RPM_33, RPM_45}
    assert set(quantize(energy, cfg)).issubset(allowed)


def test_tapestop_three_states():
    cfg = QuantizeConfig(thr_low=1.0, thr_high=2.0, enable_tapestop=True)
    energy = [0.5, 1.5, 2.5]
    assert quantize(energy, cfg) == [RPM_STOP, RPM_33, RPM_45]


def test_hysteresis_kills_chatter():
    # Values hovering right at the 33/45 boundary should not flip every step.
    cfg = QuantizeConfig(thr_low=0.0, thr_high=1.0, hysteresis=0.3)
    energy = [1.1, 0.9, 1.1, 0.9, 1.1]  # +-0.1 around 1.0, inside the 0.3 margin
    out = quantize(energy, cfg)
    # First sample crosses thr_high+h? 1.1 < 1.3 so starts LOW and stays LOW (never reaches 1.3).
    assert out == [RPM_33] * 5


def test_hysteresis_allows_real_moves():
    cfg = QuantizeConfig(thr_low=0.0, thr_high=1.0, hysteresis=0.2)
    energy = [0.0, 1.5, 1.5, 0.5, 0.0]  # clearly above 1.2 then clearly below 0.8
    out = quantize(energy, cfg)
    assert out == [RPM_33, RPM_45, RPM_45, RPM_33, RPM_33]


def test_min_hold_debounce():
    states = [0, 0, 1, 0, 0]  # the single "1" is shorter than min_hold=2
    assert _debounce(states, 2) == [0, 0, 0, 0, 0]


def test_min_hold_keeps_long_runs():
    states = [0, 0, 1, 1, 0, 0]
    assert _debounce(states, 2) == [0, 0, 1, 1, 0, 0]


def test_min_hold_via_quantize():
    cfg = QuantizeConfig(thr_low=0.0, thr_high=1.0, min_hold=3)
    energy = [0.0, 2.0, 0.0, 0.0, 0.0]  # lone spike to 45 should be suppressed
    out = quantize(energy, cfg)
    assert out == [RPM_33] * 5


def test_downsample_counts():
    # 30 fps, 120 bpm, 16th notes => 8 steps/sec => 3.75 frames/step.
    energy = [1.0] * 30  # ~1 second
    steps = downsample(energy, src_fps=30.0, bpm=120.0, grid=16)
    assert len(steps) == 8
    assert all(abs(s - 1.0) < 1e-9 for s in steps)


def test_downsample_max_agg():
    energy = [0.0, 0.0, 5.0, 0.0]
    steps = downsample(energy, src_fps=4.0, bpm=120.0, grid=16, agg="max")
    # 4 fps, 8 steps/sec -> 0.5 frames/step -> 8 steps, the spike survives under max.
    assert max(steps) == 5.0


def test_percentile():
    assert percentile([0, 10], 50) == 5.0
    assert percentile([1, 2, 3, 4], 0) == 1
    assert percentile([1, 2, 3, 4], 100) == 4


def test_suggest_thresholds_ordering():
    energy = list(range(100))
    low, high = suggest_thresholds(energy, 50, 80)
    assert low < high


def test_midi_header_well_formed():
    rpm = [RPM_33, RPM_45, RPM_45, RPM_33]
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "x.mid")
        export.write_midi(path, rpm, bpm=120.0, grid=16)
        with open(path, "rb") as fh:
            data = fh.read()
    assert data[:4] == b"MThd"
    fmt, ntrk, ppq = struct.unpack(">HHH", data[8:14])
    assert (fmt, ntrk, ppq) == (0, 1, export.PPQ)
    assert b"MTrk" in data
    assert data.endswith(b"\xFF\x2F\x00")


def test_json_payload_shape():
    rpm = [RPM_45, RPM_33]
    payload = export.to_dict(rpm, bpm=120.0, grid=16, fps=30.0)
    assert payload["format"] == "turntable-lfo"
    assert payload["rpm"] == [45.0, 33.333]
    assert payload["steps"] == 2
    assert payload["ref_rpm"] == RPM_45


if __name__ == "__main__":
    import traceback

    funcs = [v for k, v in sorted(globals().items()) if k.startswith("test_") and callable(v)]
    failed = 0
    for fn in funcs:
        try:
            fn()
            print(f"PASS {fn.__name__}")
        except Exception:  # noqa: BLE001
            failed += 1
            print(f"FAIL {fn.__name__}")
            traceback.print_exc()
    print(f"\n{len(funcs) - failed}/{len(funcs)} passed")
    raise SystemExit(1 if failed else 0)

"""Serialize a quantized RPM step-LFO to formats the VST / a DAW can read.

JSON is the canonical format consumed by the Turntable LFO VST. A minimal Standard MIDI
File (type 0) writer is included so the curve can drop into a DAW as CC automation with no
third-party dependency.
"""

from __future__ import annotations

import json
import struct
from typing import Dict, Sequence

from .quantize import RPM_45

PPQ = 480  # MIDI ticks per quarter note


def to_dict(
    rpm: Sequence[float],
    bpm: float,
    grid: int,
    fps: float,
    ref_rpm: float = RPM_45,
) -> Dict:
    """Build the canonical JSON payload. ``ref_rpm`` documents the varispeed reference."""
    return {
        "format": "turntable-lfo",
        "version": 1,
        "bpm": bpm,
        "grid": grid,          # note division: 16 == 16th-note steps
        "fps": fps,            # source video fps (provenance)
        "ref_rpm": ref_rpm,    # rate = rpm / ref_rpm  (45 => unity)
        "steps": len(rpm),
        "rpm": [round(v, 3) for v in rpm],
    }


def write_json(path: str, payload: Dict) -> None:
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=2)


# --- minimal Standard MIDI File (SMF) writer -----------------------------------------------

def _vlq(value: int) -> bytes:
    """Encode an int as a MIDI variable-length quantity."""
    if value < 0:
        raise ValueError("vlq value must be >= 0")
    buf = [value & 0x7F]
    value >>= 7
    while value:
        buf.append((value & 0x7F) | 0x80)
        value >>= 7
    return bytes(reversed(buf))


def rpm_to_cc(rpm: float, ref_rpm: float = RPM_45) -> int:
    """Map an RPM value to a 0..127 CC value (linear against ref_rpm)."""
    cc = round((rpm / ref_rpm) * 127.0)
    return max(0, min(127, cc))


def write_midi(
    path: str,
    rpm: Sequence[float],
    bpm: float,
    grid: int,
    cc: int = 1,
    channel: int = 0,
    ref_rpm: float = RPM_45,
) -> None:
    """Write the step-LFO as one CC automation lane in a type-0 SMF.

    Each grid step emits a CC event; the held value encodes the RPM (see ``rpm_to_cc``).
    """
    ticks_per_step = int(round(PPQ * 4 / grid))
    micros_per_quarter = int(round(60_000_000 / bpm))

    events = bytearray()
    # Tempo meta event at tick 0.
    events += _vlq(0) + b"\xFF\x51\x03" + struct.pack(">I", micros_per_quarter)[1:]

    anchor_tick = 0      # absolute tick of the last event actually written
    last_cc = None
    for i, value in enumerate(rpm):
        cc_val = rpm_to_cc(value, ref_rpm)
        if cc_val == last_cc:
            continue  # only emit on change; skipped steps fold into the next delta
        event_tick = i * ticks_per_step
        events += _vlq(event_tick - anchor_tick) + bytes([0xB0 | (channel & 0x0F), cc & 0x7F, cc_val])
        anchor_tick = event_tick
        last_cc = cc_val
    # End-of-track, one step after the final grid position.
    end_tick = len(rpm) * ticks_per_step
    events += _vlq(max(0, end_tick - anchor_tick)) + b"\xFF\x2F\x00"

    track = b"MTrk" + struct.pack(">I", len(events)) + bytes(events)
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ)
    with open(path, "wb") as fh:
        fh.write(header + track)

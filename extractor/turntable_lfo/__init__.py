"""turntable_lfo: extract a hard-quantized turntable-RPM step-LFO from raw video.

Pipeline: video -> motion energy (OpenCV) -> BPM-grid downsample -> Schmitt-trigger
quantize to 33 1/3 / 45 (/ 0) rpm -> JSON/MIDI for the Turntable LFO VST.
"""

from .quantize import (
    RPM_33,
    RPM_45,
    RPM_STOP,
    QuantizeConfig,
    downsample,
    quantize,
    suggest_thresholds,
)

__all__ = [
    "RPM_STOP",
    "RPM_33",
    "RPM_45",
    "QuantizeConfig",
    "quantize",
    "downsample",
    "suggest_thresholds",
]

__version__ = "0.1.0"

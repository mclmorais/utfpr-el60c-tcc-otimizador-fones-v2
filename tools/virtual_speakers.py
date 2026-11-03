#!/usr/bin/env python3
"""Generate the Virtual speakers filters from a SADIE II SOFA file.

Reads the head-related impulse responses of the SOFA file, takes the speaker
at +30 degrees azimuth and 0 degrees elevation (the listener's left) and
mirrors it for the right one, assuming a symmetric head. The shuffler then
needs only two filters: the sum of the channels goes through
ipsilateral + contralateral and the difference through ipsilateral -
contralateral. Both are cut and windowed to the tap count, scaled so a mono
pink noise keeps its K-weighted loudness (ITU-R BS.1770), or lower if the mono
peak would otherwise pass +3 dB, and written as a C header of
single-precision constants.

The SADIE II files are already diffuse-field equalised (their History
attribute says so), so the script uses them as they come. The same SOFA file
always produces the same header; the build never runs this script.

Needs the Python packages numpy and h5py.

Usage:
  tools/virtual_speakers.py SOFA                # write Application/DSP/Inc/virtual_speakers_filters.h
  tools/virtual_speakers.py SOFA --taps 96      # another tap count
  tools/virtual_speakers.py SOFA --output FILE  # write somewhere else
"""

import argparse
import hashlib
import os
import sys
from dataclasses import dataclass

import h5py
import numpy as np
from loudness import PINK_BAND_HZ, SAMPLE_RATE, k_weighting_power

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "Application", "DSP", "Inc", "virtual_speakers_filters.h")

SPEAKER_AZIMUTH = 30
DEFAULT_TAPS = 128
# Raised-cosine fade at each end of the cut, as a fraction of the tap count.
FADE_FRACTION = 1 / 8
# The mono response may rise this far above 0 dB before the loudness gain is lowered.
PEAK_LIMIT_DB = 3.0
ANALYSIS_POINTS = 1 << 16


@dataclass
class Hrirs:
    ipsilateral: np.ndarray
    contralateral: np.ndarray
    sha256: str


@dataclass
class Filters:
    taps: int
    sum: np.ndarray
    difference: np.ndarray
    gain_db: float
    peak_db: float
    peak_limited: bool


def read_hrirs(path):
    """The left speaker's responses at the left (ipsilateral) and right (contralateral) ears."""
    with open(path, "rb") as f:
        sha256 = hashlib.sha256(f.read()).hexdigest()
    with h5py.File(path, "r") as sofa:
        conventions = sofa.attrs.get("SOFAConventions", b"").decode()
        if conventions != "SimpleFreeFieldHRIR":
            sys.exit(f"{path}: SOFAConventions is {conventions!r}, expected 'SimpleFreeFieldHRIR'")
        rate = float(sofa["Data.SamplingRate"][0])
        if rate != SAMPLE_RATE:
            sys.exit(f"{path}: sampled at {rate:g} Hz, the DSP runs at {SAMPLE_RATE} Hz")
        positions = sofa["SourcePosition"][...]
        matches = np.flatnonzero((positions[:, 0] == SPEAKER_AZIMUTH) & (positions[:, 1] == 0))
        if len(matches) != 1:
            sys.exit(f"{path}: {len(matches)} measurements at {SPEAKER_AZIMUTH} degrees azimuth, 0 elevation")
        # Receiver 0 is the left ear; a positive azimuth is to the listener's left.
        ir = sofa["Data.IR"][matches[0]]
    return Hrirs(ir[0].astype(np.float64), ir[1].astype(np.float64), sha256)


def mono_loudness_db(sum_filter):
    """K-weighted level of a mono pink noise after the sum filter, relative to the noise itself."""
    frequencies = np.fft.rfftfreq(ANALYSIS_POINTS, 1 / SAMPLE_RATE)
    band = (frequencies >= PINK_BAND_HZ[0]) & (frequencies <= PINK_BAND_HZ[1])
    weight = k_weighting_power(frequencies[band]) / frequencies[band]
    response = np.abs(np.fft.rfft(sum_filter, ANALYSIS_POINTS)[band]) ** 2
    return 10 * np.log10(np.sum(weight * response) / np.sum(weight))


def peak_db(sum_filter):
    return 20 * np.log10(np.max(np.abs(np.fft.rfft(sum_filter, ANALYSIS_POINTS))))


def cut(response, start, taps):
    fade = int(taps * FADE_FRACTION)
    window = np.ones(taps)
    ramp = 0.5 - 0.5 * np.cos(np.pi * (np.arange(fade) + 0.5) / fade)
    window[:fade] = ramp
    window[taps - fade :] = ramp[::-1]
    return response[start : start + taps] * window


def design(hrirs, taps):
    """The shuffler filters cut to taps, placed where they keep the most energy, at mono loudness."""
    full_sum = hrirs.ipsilateral + hrirs.contralateral
    full_difference = hrirs.ipsilateral - hrirs.contralateral
    length = len(full_sum)
    if taps > length:
        sys.exit(f"the SOFA responses have {length} taps, fewer than {taps}")
    energy = full_sum**2 + full_difference**2
    kept = [np.sum(energy[start : start + taps]) for start in range(length - taps + 1)]
    start = int(np.argmax(kept))
    sum_filter = cut(full_sum, start, taps)
    difference_filter = cut(full_difference, start, taps)

    return normalize(sum_filter, difference_filter)


def normalize(sum_filter, difference_filter):
    """Scale the pair to mono loudness, or lower if the mono peak would pass the limit."""
    taps = len(sum_filter)
    gain_db = -mono_loudness_db(sum_filter)
    peak = peak_db(sum_filter) + gain_db
    limited = peak > PEAK_LIMIT_DB
    if limited:
        gain_db -= peak - PEAK_LIMIT_DB
        peak = PEAK_LIMIT_DB
    gain = 10 ** (gain_db / 20)
    return Filters(taps, gain * sum_filter, gain * difference_filter, gain_db, peak, limited)


def c_array(name, values):
    lines = [f"static const float {name}[VIRTUAL_SPEAKERS_TAPS] = {{"]
    for row in range(0, len(values), 4):
        # The shortest decimal that reads back as the same float, so the header is stable across numpy versions.
        lines.append("  " + " ".join(f"{np.format_float_scientific(np.float32(v), unique=True)}f,"
                                     for v in values[row : row + 4]))
    lines.append("};")
    return "\n".join(lines)


def header_text(filters, sofa_name, sha256):
    limited = (f"lowered so the mono peak stays at +{PEAK_LIMIT_DB:g} dB" if filters.peak_limited
               else f"mono peak at {filters.peak_db:+.2f} dB")
    return f"""// Generated by tools/virtual_speakers.py; do not edit. Rerun the script to change it.
//
// Source: {sofa_name}, SHA-256 {sha256}
// SADIE II database, KEMAR (D2), University of York. Copyright 2018, University of York,
// licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0).
// Armstrong, C.; Thresh, L.; Murphy, D.; Kearney, G. "A Perceptual Evaluation of Individual and
// Non-Individual HRTFs: A Case Study of the SADIE II Database." Applied Sciences 2018, 8(11), 2029,
// https://doi.org/10.3390/app8112029
// Modified: the +30 degree, 0 degree elevation pair, mirrored for -30 degrees, summed and differenced,
// cut and windowed to {filters.taps} taps and scaled by {filters.gain_db:+.2f} dB ({limited}).

#if !defined(__VIRTUAL_SPEAKERS_FILTERS_H__)
#define __VIRTUAL_SPEAKERS_FILTERS_H__

#define VIRTUAL_SPEAKERS_TAPS {filters.taps}

// Shuffler: (left + right) / 2 goes through the sum filter, (left - right) / 2 through the difference
// filter; left is their sum and right their difference.
{c_array("virtualSpeakersSum", filters.sum)}

{c_array("virtualSpeakersDifference", filters.difference)}

#endif // __VIRTUAL_SPEAKERS_FILTERS_H__
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sofa", help="the SADIE II HRIR SOFA file at 48 kHz")
    parser.add_argument("--taps", type=int, default=DEFAULT_TAPS, help=f"filter length (default {DEFAULT_TAPS})")
    parser.add_argument("--output", default=HEADER, help="header to write")
    args = parser.parse_args()

    hrirs = read_hrirs(args.sofa)
    filters = design(hrirs, args.taps)
    with open(args.output, "w", encoding="utf-8", newline="\n") as f:
        f.write(header_text(filters, os.path.basename(args.sofa), hrirs.sha256))
    print(f"{os.path.relpath(args.output)}: {filters.taps} taps, gain {filters.gain_db:+.2f} dB, "
          f"mono peak {filters.peak_db:+.2f} dB{' (limited)' if filters.peak_limited else ''}")


if __name__ == "__main__":
    main()

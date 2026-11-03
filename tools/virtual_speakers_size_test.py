#!/usr/bin/env python3
"""Choose the Virtual speakers filter size by measured error.

Each candidate size is cut from the SOFA responses and compared with the whole
filter, the same responses uncut. The comparison is at the two ears: the
speaker's response at the near ear (ipsilateral) and at the far ear
(contralateral), rebuilt from the sum and difference filters. The size passes
when both of these hold:

  - the magnitude stays within 1 dB of the whole filter from 200 Hz to 16 kHz,
    after smoothing to 1/6 octave, the choice the ticket did not specify. The
    error without smoothing is printed beside it; it moves each size by less
    than 0.4 dB and picks the same one;
  - the delay between the ears is within 1 sample (21 us) of the whole filter.

The test prints the error of every size, names the smallest that passes, and
fails when the header in the repository uses another size.

Needs the SADIE II SOFA file, which the repository does not carry.

Usage:
  tools/virtual_speakers_size_test.py SOFA
"""

import argparse
import re
import sys
from dataclasses import dataclass

import numpy as np

import virtual_speakers as vs

SIZES = [64, 96, 128, 192, 256]
BAND_HZ = (200, 16000)
MAGNITUDE_LIMIT_DB = 1.0
DELAY_LIMIT_SAMPLES = 1.0
SMOOTHING_OCTAVES = 1 / 6
ANALYSIS_POINTS = vs.ANALYSIS_POINTS
DELAY_UPSAMPLING = 16


@dataclass
class SizeError:
    taps: int
    magnitude_db: float
    raw_magnitude_db: float
    delay_samples: float

    @property
    def passes(self):
        return self.magnitude_db <= MAGNITUDE_LIMIT_DB and self.delay_samples <= DELAY_LIMIT_SAMPLES


def ears(filters):
    """The speaker's impulse responses at the near and far ear."""
    return (filters.sum + filters.difference) / 2, (filters.sum - filters.difference) / 2


def smoothed_db(impulse_response, frequencies, smoothing_octaves=SMOOTHING_OCTAVES):
    """Magnitude in dB at frequencies, power-averaged over SMOOTHING_OCTAVES around each one.

    With smoothing_octaves of 0, the magnitude at the nearest FFT bin, unsmoothed.
    """
    bins = np.fft.rfftfreq(ANALYSIS_POINTS, 1 / vs.SAMPLE_RATE)
    power = np.abs(np.fft.rfft(impulse_response, ANALYSIS_POINTS)) ** 2
    total = np.concatenate(([0.0], np.cumsum(power)))
    half = 2 ** (smoothing_octaves / 2)
    low = np.searchsorted(bins, frequencies / half)
    high = np.searchsorted(bins, frequencies * half, side="right")
    if smoothing_octaves == 0:
        return 10 * np.log10(power[np.searchsorted(bins, frequencies)])
    return 10 * np.log10((total[high] - total[low]) / (high - low))


def interaural_delay(near, far):
    """How many samples the far ear lags the near ear, from the peak of their cross-correlation."""
    length = 4 * len(near)
    spectrum = np.fft.rfft(far, length) * np.conj(np.fft.rfft(near, length))
    correlation = np.fft.irfft(spectrum, DELAY_UPSAMPLING * length)
    lags = np.arange(len(correlation))
    lags = np.where(lags > len(correlation) // 2, lags - len(correlation), lags) / DELAY_UPSAMPLING
    return float(lags[np.argmax(correlation)])


def measure(hrirs, taps, whole):
    cut_near, cut_far = ears(vs.design(hrirs, taps))
    frequencies = np.geomspace(BAND_HZ[0], BAND_HZ[1], 400)
    pairs = ((cut_near, whole[0]), (cut_far, whole[1]))
    magnitude = max(np.max(np.abs(smoothed_db(c, frequencies) - smoothed_db(w, frequencies))) for c, w in pairs)
    raw = max(np.max(np.abs(smoothed_db(c, frequencies, 0) - smoothed_db(w, frequencies, 0))) for c, w in pairs)
    delay = abs(interaural_delay(cut_near, cut_far) - interaural_delay(*whole))
    return SizeError(taps, float(magnitude), float(raw), delay)


def header_taps():
    with open(vs.HEADER, encoding="utf-8") as f:
        return int(re.search(r"#define VIRTUAL_SPEAKERS_TAPS (\d+)", f.read()).group(1))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sofa", help="the SADIE II HRIR SOFA file at 48 kHz")
    args = parser.parse_args()

    hrirs = vs.read_hrirs(args.sofa)
    whole = ears(vs.design(hrirs, max(SIZES)))
    errors = [measure(hrirs, taps, whole) for taps in SIZES]

    print(f"{'taps':>5} {'smoothed 200 Hz-16 kHz':>23} {'unsmoothed':>13} {'interaural delay':>17}")
    for e in errors:
        print(f"{e.taps:5d} {e.magnitude_db:20.2f} dB {e.raw_magnitude_db:10.2f} dB"
              f" {e.delay_samples * 1e6 / vs.SAMPLE_RATE:13.1f} us"
              f"  {'passes' if e.passes else 'fails'}")
    chosen = next(e.taps for e in errors if e.passes)
    print(f"chosen: {chosen} taps (limits {MAGNITUDE_LIMIT_DB:g} dB, {DELAY_LIMIT_SAMPLES:g} sample)")

    if header_taps() != chosen:
        sys.exit(f"FAIL: the header has {header_taps()} taps; run tools/virtual_speakers.py SOFA --taps {chosen}")
    print("PASS: the header uses the chosen size")


if __name__ == "__main__":
    main()

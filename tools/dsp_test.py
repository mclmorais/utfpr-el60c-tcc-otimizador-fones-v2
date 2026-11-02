#!/usr/bin/env python3
"""Check the DSP response on the PC, without the board.

Compiles the sources under Application/DSP/ as a shared library with the C
compiler from the ziglang pip package, loads it with ctypes and feeds it the
same packets the USB OUT callback does: interleaved stereo 16-bit frames,
processed in place. Settings go through the public state the screens change,
never through a filter or its coefficients. A failing case prints which
response came out wrong and the script exits with a non-zero code.

Needs the Python packages ziglang and numpy; --plot also needs matplotlib.

Usage:
  tools/dsp_test.py              # run every case
  tools/dsp_test.py --plot DIR   # also save a PNG of each measured response in DIR
  tools/dsp_test.py NAME...      # run only the cases whose name contains NAME
"""

import argparse
import ctypes
import glob
import os
import subprocess
import sys
from dataclasses import dataclass

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DSP_DIR = os.path.join(ROOT, "Application", "DSP")
# Stand-ins for the board code the DSP calls, such as the DWT cycle counter.
PC_STUBS_DIR = os.path.join(ROOT, "tools", "dsp_pc")
BUILD_DIR = os.path.join(ROOT, "build", "pc")

SAMPLE_RATE = 48000
FRAMES_PER_PACKET = SAMPLE_RATE // 1000
BAND_COUNT = 31
GAIN_STEPS_PER_DB = 10
# The 20 Hz band rings for about 70 ms; one second leaves it far below the 16-bit floor.
SETTLE_PACKETS = 1000
# Every band centre is a multiple of 0.5 Hz, so two seconds hold a whole number of cycles of each.
MEASURE_FRAMES = 2 * SAMPLE_RATE
TOLERANCE_DB = 0.1


class Failure(Exception):
    pass


@dataclass
class Curve:
    label: str
    frequencies: np.ndarray
    measured_db: np.ndarray
    expected_db: np.ndarray


@dataclass
class Plot:
    title: str
    curves: list


def build():
    os.makedirs(BUILD_DIR, exist_ok=True)
    suffix = {"win32": ".dll", "darwin": ".dylib"}.get(sys.platform, ".so")
    library = os.path.join(BUILD_DIR, "dsp" + suffix)
    sources = sorted(glob.glob(os.path.join(DSP_DIR, "Src", "*.c")) + glob.glob(os.path.join(PC_STUBS_DIR, "*.c")))
    command = [sys.executable, "-m", "ziglang", "cc", "-shared", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror"]
    command += ["-I", os.path.join(DSP_DIR, "Inc"), *sources, "-o", library]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("the DSP sources do not compile on the PC:\n" + result.stdout + result.stderr)
    return library


class EqBand(ctypes.Structure):
    _fields_ = [("frequency", ctypes.c_float), ("label", ctypes.c_char_p)]


class Dsp:
    """The firmware DSP as the USB callback and the screens see it."""

    def __init__(self, library):
        self.lib = ctypes.CDLL(library)
        self.lib.AudioUserDsp_Process.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32]
        self.gains = self.state("eqGains", ctypes.c_int16 * BAND_COUNT)
        self.enabled = self.state("eqEnabled", ctypes.c_bool)
        self.band_centres = [band.frequency for band in self.state("eqBands", EqBand * BAND_COUNT)]

    def state(self, name, ctype):
        return ctype.in_dll(self.lib, name)

    def set_gains_db(self, gains_db):
        for band, gain in enumerate(gains_db):
            self.gains[band] = round(gain * GAIN_STEPS_PER_DB)

    def settle(self):
        """Start from the current settings with every per-packet transition finished."""
        self.lib.AudioUserDsp_Init()
        self.process(np.zeros((SETTLE_PACKETS * FRAMES_PER_PACKET, 2), dtype=np.int16))

    def process(self, frames):
        """Run (n, 2) int16 frames through the DSP one USB packet at a time."""
        packets = np.ascontiguousarray(frames, dtype="<i2").copy()
        buffer = packets.reshape(-1).view(np.uint8)
        packet_bytes = FRAMES_PER_PACKET * 4
        for offset in range(0, len(buffer), packet_bytes):
            packet = buffer[offset : offset + packet_bytes]
            self.lib.AudioUserDsp_Process(packet.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8)), len(packet))
        return packets


def response_db(dsp, frequencies, amplitude=1000):
    """Steady-state gain of each channel at each frequency, all measured together in one multitone run.

    Every frequency must be a multiple of 0.5 Hz: the tones then hold whole cycles in the measured
    window and are orthogonal, so each projection sees only its own tone.
    """
    frames = SETTLE_PACKETS * FRAMES_PER_PACKET + MEASURE_FRAMES
    phases = 2 * np.pi * np.outer(np.arange(frames) / SAMPLE_RATE, frequencies)
    tone = np.round(amplitude * np.sin(phases).sum(axis=1))
    output = dsp.process(np.stack([tone, tone], axis=1))[-MEASURE_FRAMES:].astype(float)
    window = phases[-MEASURE_FRAMES:]
    basis = np.exp(-1j * window)
    reference = np.abs(2 * tone[-MEASURE_FRAMES:] @ basis / MEASURE_FRAMES)
    return [20 * np.log10(np.abs(2 * output[:, channel] @ basis / MEASURE_FRAMES) / reference) for channel in range(2)]


def peaking_eq_db(centre, gain, frequencies):
    """Peaking EQ magnitude from the Audio EQ Cookbook (R. Bristow-Johnson), BW = 1/3 octave."""
    a = 10 ** (gain / 40)
    w0 = 2 * np.pi * centre / SAMPLE_RATE
    alpha = np.sin(w0) * np.sinh(np.log(2) / 2 * (1 / 3) * w0 / np.sin(w0))
    b = np.array([1 + alpha * a, -2 * np.cos(w0), 1 - alpha * a])
    den = np.array([1 + alpha / a, -2 * np.cos(w0), 1 - alpha / a])
    z = np.exp(-1j * 2 * np.pi * np.asarray(frequencies) / SAMPLE_RATE)
    response = (b[0] + b[1] * z + b[2] * z**2) / (den[0] + den[1] * z + den[2] * z**2)
    return 20 * np.log10(np.abs(response))


# A case takes the Dsp, raises Failure naming each wrong response, and returns the Plots it measured.
CASES = []


def case(function):
    CASES.append(function)
    return function


@case
def flat_bands_are_bit_exact(dsp):
    dsp.set_gains_db([0] * BAND_COUNT)
    dsp.settle()
    rng = np.random.default_rng(0)
    noise = rng.integers(-32768, 32768, size=(SAMPLE_RATE, 2), dtype=np.int16)
    edges = np.array([[32767, -32768], [-32768, 32767], [0, 0], [-1, 1]] * 12, dtype=np.int16)
    frames = np.concatenate([noise, edges])
    output = dsp.process(frames)
    differ = np.flatnonzero(np.any(output != frames, axis=1))
    if len(differ):
        first = differ[0]
        raise Failure(f"{len(differ)} frames changed with every band at 0 dB, "
                      f"first at frame {first}: {frames[first].tolist()} -> {output[first].tolist()}")
    return []


def alternating_gains_db():
    return [6 if band % 2 else -6 for band in range(BAND_COUNT)]


@case
def disabled_equalizer_is_bit_exact(dsp):
    dsp.set_gains_db(alternating_gains_db())
    dsp.enabled.value = False
    try:
        dsp.settle()
        frames = np.random.default_rng(1).integers(-32768, 32768, size=(SAMPLE_RATE, 2), dtype=np.int16)
        output = dsp.process(frames)
    finally:
        dsp.enabled.value = True
    differ = np.flatnonzero(np.any(output != frames, axis=1))
    if len(differ):
        first = differ[0]
        raise Failure(f"{len(differ)} frames changed with the Equalizer off and bands at +-6 dB, "
                      f"first at frame {first}: {frames[first].tolist()} -> {output[first].tolist()}")
    return []


@case
def switching_never_jumps_more_than_the_signal(dsp):
    """Off and back on with a boosted 1 kHz tone, switched at its peak, where on and off differ most."""
    band = dsp.band_centres.index(1000)
    gains = [0] * BAND_COUNT
    gains[band] = 6
    dsp.set_gains_db(gains)
    segment = 200 * FRAMES_PER_PACKET
    tone = np.round(8000 * np.cos(2 * np.pi * 1000 * np.arange(3 * segment) / SAMPLE_RATE))
    frames = np.stack([tone, tone], axis=1).astype(np.int16)

    dsp.settle()
    always_on = dsp.process(frames).astype(int)
    dsp.settle()
    output = dsp.process(frames[:segment])
    dsp.enabled.value = False
    output = np.concatenate([output, dsp.process(frames[segment : 2 * segment])])
    dsp.enabled.value = True
    output = np.concatenate([output, dsp.process(frames[2 * segment :])]).astype(int)

    steps = np.abs(np.diff(output, axis=0))
    switches = [segment, 2 * segment]
    # Step k goes from frame k to k + 1, so the steps touching a fade packet start one frame before it.
    near_switch = np.zeros(len(steps), dtype=bool)
    for switch in switches:
        near_switch[switch - 1 : switch + FRAMES_PER_PACKET + 1] = True
    signal_step = steps[~near_switch].max(axis=0)
    switch_step = steps[near_switch].max(axis=0)

    errors = []
    for channel, name in enumerate(("left", "right")):
        if switch_step[channel] > signal_step[channel]:
            errors.append(f"{name}: switching steps by {switch_step[channel]}, "
                          f"the signal itself by at most {signal_step[channel]}")
    after_fade = 2 * segment + FRAMES_PER_PACKET
    if np.any(output[after_fade:] != always_on[after_fade:]):
        errors.append("after switching back on, the output differs from a run that stayed on: "
                      "the filters stopped following the signal while off")
    if errors:
        raise Failure("; ".join(errors))
    return []


@case
def boosted_band_follows_cookbook(dsp):
    """Each band alone at +6 dB: +6 dB at its centre and the cookbook curve one octave around it."""
    errors, curves = [], []
    for band, centre in enumerate(dsp.band_centres):
        gains = [0] * BAND_COUNT
        gains[band] = 6
        dsp.set_gains_db(gains)
        dsp.settle()
        steps = np.arange(-6, 7)
        frequencies = np.unique(np.round(centre * 2.0 ** (steps / 6) * 2) / 2)
        frequencies = frequencies[frequencies < SAMPLE_RATE / 2 * 0.95]
        expected = np.where(frequencies == centre, 6.0, peaking_eq_db(centre, 6, frequencies))
        left, right = response_db(dsp, frequencies)
        for channel, measured in (("left", left), ("right", right)):
            for frequency, got, want in zip(frequencies, measured, expected):
                if abs(got - want) > TOLERANCE_DB:
                    errors.append(f"band {centre:g} Hz at +6 dB, {channel} at {frequency:g} Hz: "
                                  f"{got:+.2f} dB, expected {want:+.2f} dB")
        curves.append(Curve(f"{centre:g} Hz", frequencies, left, expected))
    if errors:
        raise Failure(f"{len(errors)} wrong responses:\n    " + "\n    ".join(errors))
    return [Plot("Cada banda sozinha em +6 dB", curves)]


def save_plots(name, plots, directory):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    for index, plot in enumerate(plots):
        figure, axes = plt.subplots(figsize=(10, 5))
        for curve in plot.curves:
            line = axes.semilogx(curve.frequencies, curve.measured_db, "o", markersize=3, label=curve.label)[0]
            axes.semilogx(curve.frequencies, curve.expected_db, "-", color=line.get_color(), linewidth=0.8)
        axes.set_title(f"{plot.title} (pontos: medido, linhas: esperado)")
        axes.set_xlabel("Frequência (Hz)")
        axes.set_ylabel("Ganho (dB)")
        axes.grid(True, which="both", alpha=0.3)
        path = os.path.join(directory, f"{name}{'' if len(plots) == 1 else f'_{index}'}.png")
        figure.savefig(path, dpi=150, bbox_inches="tight")
        plt.close(figure)
        print(f"     plot: {os.path.relpath(path)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--plot", metavar="DIR", help="save a PNG of each measured response in DIR")
    parser.add_argument("names", nargs="*", help="run only the cases whose name contains one of these")
    args = parser.parse_args()
    if args.plot:
        try:
            import matplotlib  # noqa: F401
        except ImportError:
            sys.exit("--plot needs matplotlib: pip install matplotlib")
        os.makedirs(args.plot, exist_ok=True)

    dsp = Dsp(build())
    selected = [c for c in CASES if not args.names or any(n in c.__name__ for n in args.names)]
    failed = 0
    for function in selected:
        try:
            plots = function(dsp)
        except Failure as failure:
            failed += 1
            print(f"FAIL {function.__name__}: {failure}")
            continue
        print(f"ok   {function.__name__}")
        if args.plot and plots:
            save_plots(function.__name__, plots, args.plot)
    print(f"{len(selected) - failed} passed, {failed} failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

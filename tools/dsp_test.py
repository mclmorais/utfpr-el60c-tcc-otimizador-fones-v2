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
import re
import subprocess
import sys
from contextlib import contextmanager
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


class CrossfeedPreset(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char_p), ("cutoff_hz", ctypes.c_float), ("level_db", ctypes.c_float)]


@dataclass
class Preset:
    index: int
    name: str
    cutoff_hz: float
    level_db: float


# The Presets by the name the firmware gives them (the LCD fonts are ASCII only), in table order.
# The first three are published by bs2b; Forte is this project's own, so it has no source to check against.
PRESET_VALUES = {"Padrao": (700, 4.5), "Chu Moy": (700, 6.0), "Jan Meier": (650, 9.5), "Forte": (1000, 3.0)}


def preset_count():
    """CROSSFEED_PRESET_COUNT, which only exists as a macro in the header."""
    with open(os.path.join(DSP_DIR, "Inc", "virtualizer.h")) as header:
        return int(re.search(r"#define CROSSFEED_PRESET_COUNT (\d+)", header.read()).group(1))


class Dsp:
    """The firmware DSP as the USB callback and the screens see it."""

    def __init__(self, library):
        self.lib = ctypes.CDLL(library)
        self.lib.AudioUserDsp_Process.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32]
        self.gains = self.state("eqGains", ctypes.c_int16 * BAND_COUNT)
        self.enabled = self.state("eqEnabled", ctypes.c_bool)
        self.band_centres = [band.frequency for band in self.state("eqBands", EqBand * BAND_COUNT)]
        self.virt_enabled = self.state("virtEnabled", ctypes.c_bool)
        self.preset = self.state("crossfeedPreset", ctypes.c_uint8)
        self.technique = self.state("virtTechnique", ctypes.c_int)
        count = preset_count()
        table = self.state("crossfeedPresets", CrossfeedPreset * count)
        self.presets = [Preset(i, p.name.decode(), p.cutoff_hz, p.level_db) for i, p in enumerate(table)]

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
    """Steady-state gain of each channel at each frequency, the same tones on both, all measured together.

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


SWITCH_SEGMENT = 200 * FRAMES_PER_PACKET


def switching_errors(dsp, switch, frames):
    """Run frames with switch on, off for the middle third and on again, switched at packet boundaries.

    Pick frames where on and off differ most at the switches. Returns what went wrong.
    """
    segment = SWITCH_SEGMENT
    dsp.settle()
    always_on = dsp.process(frames).astype(int)
    dsp.settle()
    output = dsp.process(frames[:segment])
    switch.value = False
    output = np.concatenate([output, dsp.process(frames[segment : 2 * segment])])
    switch.value = True
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
    return errors


def cosine(frequency, frames, amplitude=8000):
    return np.round(amplitude * np.cos(2 * np.pi * frequency * np.arange(frames) / SAMPLE_RATE))


@case
def switching_never_jumps_more_than_the_signal(dsp):
    """Off and back on with a boosted 1 kHz tone, switched at its peak, where on and off differ most."""
    band = dsp.band_centres.index(1000)
    gains = [0] * BAND_COUNT
    gains[band] = 6
    dsp.set_gains_db(gains)
    tone = cosine(1000, 3 * SWITCH_SEGMENT)
    errors = switching_errors(dsp, dsp.enabled, np.stack([tone, tone], axis=1).astype(np.int16))
    if errors:
        raise Failure("; ".join(errors))
    return []


@case
def empty_packet_does_not_consume_the_fade(dsp):
    """An empty packet on the switch edge must leave the fade for the next packet that has audio."""
    band = dsp.band_centres.index(1000)
    gains = [0] * BAND_COUNT
    gains[band] = 6
    dsp.set_gains_db(gains)
    tone = cosine(1000, 2 * SWITCH_SEGMENT)
    frames = np.stack([tone, tone], axis=1).astype(np.int16)
    dsp.settle()
    output = dsp.process(frames[:SWITCH_SEGMENT])
    dsp.enabled.value = False
    try:
        empty = np.zeros(4, dtype=np.uint8)
        dsp.lib.AudioUserDsp_Process(empty.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8)), 0)
        output = np.concatenate([output, dsp.process(frames[SWITCH_SEGMENT:])]).astype(int)
    finally:
        dsp.enabled.value = True
    steps = np.abs(np.diff(output, axis=0))
    near_switch = np.zeros(len(steps), dtype=bool)
    near_switch[SWITCH_SEGMENT - 1 : SWITCH_SEGMENT + FRAMES_PER_PACKET + 1] = True
    signal_step = steps[~near_switch].max(axis=0)
    switch_step = steps[near_switch].max(axis=0)
    if np.any(switch_step > signal_step):
        raise Failure(f"switching after an empty packet steps by {switch_step.tolist()}, "
                      f"the signal itself by at most {signal_step.tolist()}")
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


# The VirtualizerTechnique values, which the flash will store.
CROSSFEED = 0
VIRTUAL_SPEAKERS = 1


@contextmanager
def virtualizer_on(dsp, preset=None, technique=CROSSFEED):
    """The Virtualization on with this Technique (and Preset) and the Equalizer flat, settled; switched back off after."""
    dsp.set_gains_db([0] * BAND_COUNT)
    dsp.preset.value = preset.index if preset else 0
    dsp.technique.value = technique
    dsp.virt_enabled.value = True
    try:
        dsp.settle()
        yield
    finally:
        dsp.virt_enabled.value = False
        dsp.technique.value = CROSSFEED
        dsp.preset.value = 0


def raise_errors(errors):
    if errors:
        raise Failure(f"{len(errors)} wrong responses:\n    " + "\n    ".join(errors))


@case
def presets_have_the_documented_values(dsp):
    errors = []
    if [p.name for p in dsp.presets] != list(PRESET_VALUES):
        errors.append(f"the Presets are {[p.name for p in dsp.presets]}, expected {list(PRESET_VALUES)} in that order "
                      f"(the index is stored in flash)")
    for preset in dsp.presets:
        expected = PRESET_VALUES.get(preset.name)
        if expected is not None and (preset.cutoff_hz, preset.level_db) != expected:
            errors.append(f"Preset {preset.name}: {preset.cutoff_hz:g} Hz, {preset.level_db:g} dB, "
                          f"documented as {expected[0]:g} Hz, {expected[1]:g} dB")
    if dsp.presets[0].name != "Padrao":
        errors.append(f"the first Preset, which an erased flash word loads, is {dsp.presets[0].name}, not Padrao")
    raise_errors(errors)
    return []


@case
def crossfeed_keeps_mono_level(dsp):
    """A mono signal keeps 0 dB at low frequencies and stays within 1 dB from 20 Hz to 20 kHz (ADR 0001)."""
    errors, curves = [], []
    frequencies = np.array(dsp.band_centres)
    for preset in dsp.presets:
        with virtualizer_on(dsp, preset):
            left, right = response_db(dsp, frequencies)
        for channel, measured in (("left", left), ("right", right)):
            if abs(measured[0]) > TOLERANCE_DB:
                errors.append(f"{preset.name}, {channel} at {frequencies[0]:g} Hz: {measured[0]:+.2f} dB, expected 0 dB")
            for frequency, got in zip(frequencies, measured):
                if abs(got) > 1:
                    errors.append(f"{preset.name}, {channel} at {frequency:g} Hz: {got:+.2f} dB, beyond 1 dB")
        curves.append(Curve(preset.name, frequencies, left, np.zeros(len(frequencies))))
    raise_errors(errors)
    return [Plot("Sinal mono com a Virtualização ligada", curves)]


def left_impulse_response(dsp, preset, amplitude=32767):
    """Gain of each output channel per frequency (1 Hz apart) for an impulse on the left input only."""
    frames = np.zeros((SAMPLE_RATE, 2), dtype=np.int16)
    frames[0, 0] = amplitude
    with virtualizer_on(dsp, preset):
        output = dsp.process(frames).astype(float)
    return [np.abs(np.fft.rfft(output[:, channel])) / amplitude for channel in range(2)]


@case
def left_impulse_crosses_through_lowpass(dsp):
    """The right output of a left-only impulse is a lowpass at the cutoff, the Preset's level below the direct path."""
    errors, curves = [], []
    low = 20
    for preset in dsp.presets:
        direct, crossed = left_impulse_response(dsp, preset)
        if crossed[low] < 1e-3:
            errors.append(f"{preset.name}: nothing of the left impulse reaches the right output")
            continue
        level_db = 20 * np.log10(direct[low] / crossed[low])
        if abs(level_db - preset.level_db) > TOLERANCE_DB:
            errors.append(f"{preset.name}: the crossed path is {level_db:.2f} dB below the direct one at {low} Hz, "
                          f"expected {preset.level_db:g} dB")
        corner = np.flatnonzero(crossed < crossed[low] / np.sqrt(2))[0]
        if abs(corner - preset.cutoff_hz) > 0.05 * preset.cutoff_hz:
            errors.append(f"{preset.name}: the crossed path falls 3 dB at {corner} Hz, expected near {preset.cutoff_hz:g} Hz")
        decade = int(10 * preset.cutoff_hz)
        drop_db = 20 * np.log10(crossed[low] / crossed[decade])
        if drop_db < 17:
            errors.append(f"{preset.name}: the crossed path at {decade} Hz is only {drop_db:.1f} dB below {low} Hz, "
                          "a first-order lowpass falls about 20 dB a decade above its cutoff")
        direct_high_db = 20 * np.log10(direct[decade])
        if abs(direct_high_db) > 1:
            errors.append(f"{preset.name}: the direct path at {decade} Hz is {direct_high_db:+.2f} dB, "
                          "expected the treble back near 0 dB")
        frequencies = np.arange(low, SAMPLE_RATE // 2, 10)
        curves.append(Curve(f"{preset.name} (direto)", frequencies, 20 * np.log10(direct[frequencies]),
                            20 * np.log10(direct[frequencies])))
        curves.append(Curve(f"{preset.name} (cruzado)", frequencies, 20 * np.log10(crossed[frequencies]),
                            20 * np.log10(crossed[frequencies])))
    raise_errors(errors)
    return [Plot("Impulso só na esquerda", curves)]


@case
def mirrored_input_mirrors_output(dsp):
    frames = np.random.default_rng(2).integers(-32768, 32768, size=(SAMPLE_RATE, 2), dtype=np.int16)
    errors = []
    for preset in dsp.presets:
        with virtualizer_on(dsp, preset):
            output = dsp.process(frames)
        with virtualizer_on(dsp, preset):
            mirrored = dsp.process(frames[:, ::-1])
        differ = np.flatnonzero(np.any(mirrored[:, ::-1] != output, axis=1))
        if len(differ):
            first = differ[0]
            errors.append(f"{preset.name}: {len(differ)} frames are not mirrored, first at frame {first}: "
                          f"{output[first].tolist()} vs mirrored {mirrored[first].tolist()}")
    raise_errors(errors)
    return []


def full_scale_tone_errors(dsp, preset, frequency, right_sign):
    """Play a full-scale sine, the right channel times right_sign, and measure each channel against full scale.

    Returns (channel name, level in dB re full scale, largest deviation from the fitted sine as a fraction of full scale).
    """
    full_scale = 32767
    with virtualizer_on(dsp, preset):
        frames = SETTLE_PACKETS * FRAMES_PER_PACKET + MEASURE_FRAMES
        phase = 2 * np.pi * frequency * np.arange(frames) / SAMPLE_RATE
        tone = np.round(full_scale * np.sin(phase))
        output = dsp.process(np.stack([tone, right_sign * tone], axis=1))[-MEASURE_FRAMES:].astype(float)
    basis = np.exp(-1j * phase[-MEASURE_FRAMES:])
    results = []
    for channel, name in enumerate(("left", "right")):
        projection = 2 * output[:, channel] @ basis / MEASURE_FRAMES
        fitted = np.real(projection * np.conj(basis))
        shape_error = np.max(np.abs(output[:, channel] - fitted)) / full_scale
        results.append((name, 20 * np.log10(np.abs(projection) / full_scale), shape_error))
    return results


@case
def full_scale_mono_does_not_saturate(dsp):
    """A full-scale mono sine keeps its level within 1 dB and its shape within 1%."""
    errors = []
    frequencies = np.array(dsp.band_centres)
    for preset in dsp.presets:
        with virtualizer_on(dsp, preset):
            gains = np.maximum(*response_db(dsp, frequencies))
        frequency = frequencies[np.argmax(gains)]
        for name, level_db, shape_error in full_scale_tone_errors(dsp, preset, frequency, 1):
            if abs(level_db) > 1 or shape_error > 0.01:
                errors.append(f"{preset.name}, {name} at {frequency:g} Hz: level {level_db:+.2f} dB, "
                              f"off the sine by up to {100 * shape_error:.2f}% of full scale")
    raise_errors(errors)
    return []


@case
def full_scale_antiphase_does_not_saturate(dsp):
    """A full-scale sine with the right channel inverted never rises above full scale or clips.

    Mono passes unchanged, so only an inverted pair gives the crossed path anything to add.
    """
    errors = []
    for preset in dsp.presets:
        for frequency in (100, 1000, dsp.band_centres[-1]):
            for name, level_db, shape_error in full_scale_tone_errors(dsp, preset, frequency, -1):
                if level_db > 1 or shape_error > 0.01:
                    errors.append(f"{preset.name}, {name} at {frequency:g} Hz: level {level_db:+.2f} dB, "
                                  f"off the sine by up to {100 * shape_error:.2f}% of full scale")
    raise_errors(errors)
    return []


@case
def changing_preset_while_playing_reaches_the_new_response(dsp):
    """Switching Presets between packets, with noise playing, ends in the same output as starting on that Preset."""
    rng = np.random.default_rng(3)
    noise = rng.integers(-8000, 8000, size=(SAMPLE_RATE, 2), dtype=np.int16)
    errors = []
    for target in dsp.presets:
        with virtualizer_on(dsp, target):
            expected = dsp.process(noise)
        for start in dsp.presets:
            if start.index == target.index:
                continue
            with virtualizer_on(dsp, start):
                dsp.process(noise[: SAMPLE_RATE // 2])
                dsp.preset.value = target.index
                output = dsp.process(noise)
            tail = slice(-SAMPLE_RATE // 4, None)
            worst = np.max(np.abs(output[tail].astype(int) - expected[tail].astype(int)))
            if worst > 2:
                errors.append(f"{start.name} to {target.name}: a quarter second later the output still differs "
                              f"from {target.name} by up to {worst} LSB")
    raise_errors(errors)
    return []


@case
def switching_virtualizer_never_jumps_more_than_the_signal(dsp):
    """Off and back on with a 250 Hz tone on the left only, switched at its peak, where on and off differ most."""
    errors = []
    tone = cosine(250, 3 * SWITCH_SEGMENT)
    frames = np.stack([tone, np.zeros(len(tone))], axis=1).astype(np.int16)
    for preset in dsp.presets:
        with virtualizer_on(dsp, preset):
            errors += [f"{preset.name}, {error}" for error in switching_errors(dsp, dsp.virt_enabled, frames)]
    raise_errors(errors)
    return []


HEAD_RADIUS_M = 0.0875
SPEED_OF_SOUND_M_S = 343
SPEAKER_ANGLE_RAD = np.radians(30)
# Long enough for any head response the DSP can afford; the rest of an impulse response is silence.
IMPULSE_FRAMES = 1024


def virtual_speakers_left_impulse(dsp, amplitude=32767):
    """Both output channels, as floats, for an impulse on the left input only."""
    frames = np.zeros((SAMPLE_RATE, 2), dtype=np.int16)
    frames[0, 0] = amplitude
    with virtualizer_on(dsp, technique=VIRTUAL_SPEAKERS):
        return dsp.process(frames).astype(float)


@case
def virtual_speakers_delay_the_far_ear_as_woodworth(dsp):
    """A left-only impulse reaches the right ear later by Woodworth's r / c (theta + sin theta), within 2 samples."""
    output = virtual_speakers_left_impulse(dsp)[:IMPULSE_FRAMES]
    left, right = output[:, 0], output[:, 1]
    if not np.any(right):
        raise Failure("nothing of the left impulse reaches the right output")
    expected = HEAD_RADIUS_M / SPEED_OF_SOUND_M_S * (SPEAKER_ANGLE_RAD + np.sin(SPEAKER_ANGLE_RAD)) * SAMPLE_RATE
    correlation = np.correlate(right, left, "full")
    delay = np.argmax(correlation) - (len(left) - 1)
    if abs(delay - expected) > 2:
        raise Failure(f"the right ear lags the left by {delay} samples, Woodworth gives {expected:.1f}")
    return []


@case
def virtual_speakers_keep_the_near_ear_louder_in_the_treble(dsp):
    """From 5 to 10 kHz the head shadows the far ear: a left-only impulse is at least 6 dB louder on the left."""
    output = virtual_speakers_left_impulse(dsp)[:IMPULSE_FRAMES]
    frequencies = np.fft.rfftfreq(IMPULSE_FRAMES, 1 / SAMPLE_RATE)
    near, far = (20 * np.log10(np.abs(np.fft.rfft(output[:, channel])) + 1e-9) for channel in range(2))
    band = (frequencies >= 5000) & (frequencies <= 10000)
    difference = near[band] - far[band]
    worst = np.argmin(difference)
    if difference[worst] < 6:
        raise Failure(f"at {frequencies[band][worst]:.0f} Hz the left ear is only {difference[worst]:.1f} dB "
                      "above the right")
    return [Plot("Alto-falantes virtuais: impulso só na esquerda",
                 [Curve("esquerda", frequencies[1:], near[1:], near[1:]),
                  Curve("direita", frequencies[1:], far[1:], far[1:])])]


@case
def virtual_speakers_mirror_the_output_of_mirrored_input(dsp):
    frames = np.random.default_rng(4).integers(-32768, 32768, size=(SAMPLE_RATE, 2), dtype=np.int16)
    with virtualizer_on(dsp, technique=VIRTUAL_SPEAKERS):
        output = dsp.process(frames)
    with virtualizer_on(dsp, technique=VIRTUAL_SPEAKERS):
        mirrored = dsp.process(frames[:, ::-1])
    differ = np.flatnonzero(np.any(mirrored[:, ::-1] != output, axis=1))
    if len(differ):
        first = differ[0]
        raise Failure(f"{len(differ)} frames are not mirrored, first at frame {first}: "
                      f"{output[first].tolist()} vs mirrored {mirrored[first].tolist()}")
    return []


# K-weighting at 48 kHz, ITU-R BS.1770-4 tables 1 and 2: a high shelf, then a highpass.
K_WEIGHTING = [
    ([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585]),
    ([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621]),
]
# The mono peak may rise this far above 0 dB; beyond it the gain comes down and the loudness with it (#28).
MONO_PEAK_LIMIT_DB = 3
LOUDNESS_TOLERANCE_DB = 0.5


def k_weighted_level_db(signal):
    spectrum = np.fft.rfft(signal)
    z = np.exp(-1j * 2 * np.pi * np.fft.rfftfreq(len(signal), 1 / SAMPLE_RATE))
    power = np.abs(spectrum) ** 2
    for b, a in K_WEIGHTING:
        power *= np.abs(np.polyval(b[::-1], z) / np.polyval(a[::-1], z)) ** 2
    return 10 * np.log10(np.sum(power))


def pink_noise(frames, rms=2000, seed=5):
    """Gaussian noise with equal power per octave from 20 Hz to 20 kHz."""
    frequencies = np.fft.rfftfreq(frames, 1 / SAMPLE_RATE)
    spectrum = np.fft.rfft(np.random.default_rng(seed).standard_normal(frames))
    band = (frequencies >= 20) & (frequencies <= 20000)
    spectrum[~band] = 0
    spectrum[band] /= np.sqrt(frequencies[band])
    noise = np.fft.irfft(spectrum, frames)
    return np.round(noise * rms / np.sqrt(np.mean(noise**2)))


def virtual_speakers_mono_response(dsp, points=1 << 16):
    """Gain in dB of the mono path at points / 2 + 1 frequencies, from an impulse on both inputs."""
    frames = np.zeros((IMPULSE_FRAMES, 2), dtype=np.int16)
    frames[0] = 32767
    with virtualizer_on(dsp, technique=VIRTUAL_SPEAKERS):
        output = dsp.process(frames).astype(float)
    if np.any(output[:, 0] != output[:, 1]):
        raise Failure("a mono impulse comes out different on the two channels")
    frequencies = np.fft.rfftfreq(points, 1 / SAMPLE_RATE)
    return frequencies, 20 * np.log10(np.abs(np.fft.rfft(output[:, 0], points)) / 32767 + 1e-12)


@case
def virtual_speakers_keep_mono_loudness(dsp):
    """Mono pink noise keeps its K-weighted loudness within 0.5 dB, unless the gain had to come down for the peak.

    In that case the mono peak sits at the limit, no lower, so the gain dropped no further than the limit needs.
    """
    frames = 4 * SAMPLE_RATE
    noise = pink_noise(frames)
    mono = np.stack([noise, noise], axis=1).astype(np.int16)
    with virtualizer_on(dsp, technique=VIRTUAL_SPEAKERS):
        output = dsp.process(mono).astype(float)
    measured = slice(SAMPLE_RATE // 10, None)
    loudness_db = k_weighted_level_db(output[measured, 0]) - k_weighted_level_db(mono[measured, 0].astype(float))
    _, response = virtual_speakers_mono_response(dsp)
    peak_db = response.max()
    at_limit = abs(peak_db - MONO_PEAK_LIMIT_DB) <= 0.05
    if abs(loudness_db) > LOUDNESS_TOLERANCE_DB and not (at_limit and loudness_db < 0):
        raise Failure(f"mono pink noise comes out {loudness_db:+.2f} dB K-weighted, with the mono peak at "
                      f"{peak_db:+.2f} dB: expected within {LOUDNESS_TOLERANCE_DB} dB, or quieter only with the "
                      f"peak at the +{MONO_PEAK_LIMIT_DB} dB limit")
    print(f"     mono pink noise {loudness_db:+.2f} dB K-weighted, mono peak {peak_db:+.2f} dB")
    return []


@case
def virtual_speakers_mono_peak_stays_below_the_limit_without_clipping(dsp):
    """The mono gain peaks at +3 dB at most; a mono sine at that frequency, as loud as fits, stays a sine within 1%.

    A sine at full scale itself would clip by the peak gain: the Chain saturates at 16 bits.
    """
    frequencies, response = virtual_speakers_mono_response(dsp)
    peak = np.argmax(response)
    errors = []
    if response[peak] > MONO_PEAK_LIMIT_DB + 0.05:
        errors.append(f"the mono gain peaks at {response[peak]:+.2f} dB at {frequencies[peak]:.0f} Hz, "
                      f"above +{MONO_PEAK_LIMIT_DB} dB")
    full_scale = 32767
    frequency = round(frequencies[peak] * 2) / 2
    amplitude = full_scale * 10 ** (-max(response[peak], 0) / 20)
    with virtualizer_on(dsp, technique=VIRTUAL_SPEAKERS):
        frames = SETTLE_PACKETS * FRAMES_PER_PACKET + MEASURE_FRAMES
        phase = 2 * np.pi * frequency * np.arange(frames) / SAMPLE_RATE
        tone = np.round(amplitude * np.sin(phase))
        output = dsp.process(np.stack([tone, tone], axis=1))[-MEASURE_FRAMES:].astype(float)
    basis = np.exp(-1j * phase[-MEASURE_FRAMES:])
    for channel, name in enumerate(("left", "right")):
        projection = 2 * output[:, channel] @ basis / MEASURE_FRAMES
        shape_error = np.max(np.abs(output[:, channel] - np.real(projection * np.conj(basis)))) / full_scale
        if shape_error > 0.01:
            errors.append(f"{name} at {frequency:g} Hz, input {20 * np.log10(amplitude / full_scale):+.2f} dBFS: "
                          f"off the sine by up to {100 * shape_error:.2f}% of full scale")
    raise_errors(errors)
    return []


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

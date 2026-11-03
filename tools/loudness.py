"""K-weighted loudness (ITU-R BS.1770-4) and pink noise at 48 kHz, shared by the filter generator and the PC test.

Needs only numpy, so the PC test does not depend on h5py.
"""

import numpy as np

SAMPLE_RATE = 48000
# Tables 1 (high shelf) and 2 (highpass) of the recommendation.
K_WEIGHTING = [
    ([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585]),
    ([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621]),
]
PINK_BAND_HZ = (20, 20000)


def k_weighting_power(frequencies):
    z = np.exp(-1j * 2 * np.pi * frequencies / SAMPLE_RATE)
    power = np.ones(len(frequencies))
    for b, a in K_WEIGHTING:
        power *= np.abs(np.polyval(b[::-1], z) / np.polyval(a[::-1], z)) ** 2
    return power


def k_weighted_level_db(signal):
    power = np.abs(np.fft.rfft(signal)) ** 2 * k_weighting_power(np.fft.rfftfreq(len(signal), 1 / SAMPLE_RATE))
    return 10 * np.log10(np.sum(power))


def pink_noise(frames, rms=2000, seed=5):
    """Gaussian noise with equal power per octave across PINK_BAND_HZ."""
    frequencies = np.fft.rfftfreq(frames, 1 / SAMPLE_RATE)
    spectrum = np.fft.rfft(np.random.default_rng(seed).standard_normal(frames))
    band = (frequencies >= PINK_BAND_HZ[0]) & (frequencies <= PINK_BAND_HZ[1])
    spectrum[~band] = 0
    spectrum[band] /= np.sqrt(frequencies[band])
    noise = np.fft.irfft(spectrum, frames)
    return np.round(noise * rms / np.sqrt(np.mean(noise**2)))

#!/usr/bin/env python3
"""Independent spectral verification of rendered WAVs (NumPy + SciPy).

The C++ tests use YIN. This test uses a Hann-windowed FFT and parabolic peak
interpolation, so it cannot pass merely because the same detector is wrong
on both input and output.
"""
from pathlib import Path
import json
import sys
import numpy as np
from scipy.io import wavfile

directory = Path(sys.argv[1] if len(sys.argv) > 1 else '.')
results = []
for filename, target in [('automatic-A4.wav', 440.0), ('manual-A3-to-C4.wav', 440 * 2 ** (-9 / 12))]:
    rate, data = wavfile.read(directory / filename)
    x = data[int(rate * 1.2):int(rate * 2.2)].astype(np.float64) / 32768
    assert len(x) == rate, 'Need a full second of steady audio'
    assert np.isfinite(x).all(), 'Non-finite output'
    assert np.max(np.abs(x)) < 0.99, 'Clipped test render'
    assert np.sqrt(np.mean(x * x)) > 0.02, 'Render is silent or nearly silent'
    fft_size = 2 ** 20
    spectrum = np.abs(np.fft.rfft(x * np.hanning(len(x)), n=fft_size))
    frequencies = np.fft.rfftfreq(fft_size, 1 / rate)
    region = np.flatnonzero((frequencies > target * .8) & (frequencies < target * 1.2))
    peak = region[np.argmax(spectrum[region])]
    a, b, c = np.log(np.maximum(spectrum[peak-1:peak+2], 1e-20))
    fractional = .5 * (a - c) / (a - 2 * b + c)
    hz = (peak + fractional) * rate / fft_size
    cents = float(1200 * np.log2(hz / target))
    assert abs(cents) < 12, f'{filename}: {cents:.3f} cents off target'
    results.append(dict(file=filename, sample_rate=rate, target_hz=target,
                        measured_hz=float(hz), cents_error=cents,
                        peak=float(np.max(np.abs(x))), rms=float(np.sqrt(np.mean(x*x)))))
print(json.dumps(dict(method='Independent FFT peak interpolation', passed=True, results=results), indent=2))

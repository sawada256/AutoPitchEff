#!/usr/bin/env python3
"""Deterministic before/after render test; no pitch-tracker telemetry for audio scoring."""
import argparse
import json
import subprocess
from pathlib import Path
import numpy as np
from evaluate import write_wav, read_wav

SR = 48000

def audio_metrics(y, hz=440):
    # Analytic fundamental from a narrow FFT band, evaluated away from file edges.
    n = len(y)
    spectrum = np.fft.fft(y)
    f = np.fft.fftfreq(n, 1 / SR)
    spectrum[(f < hz * .75) | (f > hz * 1.35)] = 0
    z = np.fft.ifft(spectrum) * 2
    frequency = np.diff(np.unwrap(np.angle(z))) * SR / (2 * np.pi)
    frequency = np.convolve(frequency, np.ones(240) / 240, mode='same')
    stable = frequency[SR:-SR]
    cents = 1200 * np.log2(np.maximum(stable, 1) / hz)
    # RMS envelope modulation in 5 ms windows, captures splice dropouts too.
    amplitude = np.abs(z)[SR:-SR]
    return {'output_abs_cents_p95': float(np.percentile(abs(cents), 95)),
            'output_cents_std': float(np.std(cents)),
            'fundamental_amplitude_cv': float(np.std(amplitude) / np.mean(amplitude))}

def main():
    p = argparse.ArgumentParser()
    p.add_argument('before', type=Path)
    p.add_argument('after', type=Path)
    p.add_argument('--directory', type=Path, default=Path('build-smoothness'))
    args = p.parse_args()
    folder = args.directory.resolve(); folder.mkdir(parents=True, exist_ok=True)
    t = np.arange(SR * 5) / SR
    scenarios = {
        'fine_jitter': (440, 18 + 10 * np.sin(2*np.pi*17*t) + 4*np.sin(2*np.pi*37*t)),
        'vibrato': (440, 18 + 32*np.sin(2*np.pi*5.5*t)),
        'high_vibrato': (880, 18 + 32*np.sin(2*np.pi*5.5*t)),
        'low_vibrato': (220, 18 + 32*np.sin(2*np.pi*5.5*t)),
        'note_boundary': (440, 45 + 24*np.sin(2*np.pi*7*t)),
    }
    results = {}
    for name, (base, offset) in scenarios.items():
        hz = base * 2**(offset/1200)
        phase = np.cumsum(hz) * (2*np.pi/SR)
        # Slowly moving harmonic amplitudes exercise period splices on evolving vowels.
        x = .22*np.sin(phase) + (.10+.035*np.sin(2*np.pi*2.1*t))*np.sin(2*phase+.3)
        x += .065*np.sin(3*phase+.5) + .045*np.sin(5*phase+.2)
        source = folder / (name + '.wav'); write_wav(source, x, SR)
        results[name] = {}
        for label, renderer in [('before', args.before), ('after', args.after)]:
            target = folder / (name+'_'+label+'.wav')
            subprocess.run([str(renderer.resolve()), str(source), str(target)], check=True, capture_output=True)
            y, sr = read_wav(target)
            result = audio_metrics(y, base)
            meters = np.genfromtxt(str(target)+'.csv', delimiter=',', names=True)
            notes = meters['target_hz'][meters['time_s'] > 1]
            result['target_switches_after_1s'] = int(np.count_nonzero(abs(np.diff(notes)) > 1))
            results[name][label] = result
    (folder/'metrics.json').write_text(json.dumps(results, indent=2)+'\n')
    print(json.dumps(results, indent=2))
    assert results['note_boundary']['after']['target_switches_after_1s'] == 0, 'Target-note chatter'
    for name, row in results.items():
        assert row['after']['output_abs_cents_p95'] < (12 if name == 'low_vibrato' else 8), f'{name}: residual pitch ripple'
        assert row['after']['output_cents_std'] < row['before']['output_cents_std'], f'{name}: no improvement'

if __name__ == '__main__': main()

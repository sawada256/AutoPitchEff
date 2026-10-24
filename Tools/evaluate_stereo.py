#!/usr/bin/env python3
"""Render stereo fixtures; verify layout, linked panning, polarity and source selection."""
import argparse
import json
import subprocess
import wave
from pathlib import Path
import numpy as np


def read(path):
    with wave.open(str(path), 'rb') as f:
        channels, rate, width = f.getnchannels(), f.getframerate(), f.getsampwidth()
        data = f.readframes(f.getnframes())
    if width == 2:
        samples = np.frombuffer(data, '<i2').astype(float) / 32768
    elif width == 3:
        b = np.frombuffer(data, np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        samples = ((v ^ 0x800000) - 0x800000).astype(float) / 8388608
    else:
        raise ValueError('Expected PCM16 or PCM24')
    return samples.reshape(-1, channels), rate


def pitch(x, rate, near=440):
    x = x[-rate*2:]
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    frequencies = np.fft.rfftfreq(len(x), 1/rate)
    bins = np.flatnonzero((frequencies > near*.95) & (frequencies < near*1.05))
    k = bins[np.argmax(spec[bins])]
    a, b, c = np.log(np.maximum(spec[k-1:k+2], 1e-15))
    delta = .5*(a-c)/(a-2*b+c)
    return (k+delta)*rate/len(x)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('renderer', type=Path)
    p.add_argument('--directory', type=Path, default=Path('build-stereo-evaluation'))
    args = p.parse_args(); args.directory.mkdir(parents=True, exist_ok=True)
    rate = 48000; t = np.arange(rate*4)/rate
    phase = 2*np.pi*452*t
    vocal = .2*np.sin(phase)+.1*np.sin(2*phase+.3)+.05*np.sin(3*phase+.5)
    fixtures = [('panned', np.column_stack([vocal, vocal*.3]), 'sum'),
                ('opposite_left', np.column_stack([vocal, -vocal]), 'left'),
                ('right_only', np.column_stack([np.zeros_like(vocal), vocal]), 'sum'),
                ('opposite_sum', np.column_stack([vocal, -vocal]), 'sum')]
    results = {}
    for name, audio, source in fixtures:
        path = args.directory / (name+'.wav')
        with wave.open(str(path), 'wb') as f:
            f.setnchannels(2); f.setsampwidth(2); f.setframerate(rate)
            f.writeframes(np.round(audio*32767).astype('<i2').tobytes())
        target = args.directory / (name+'_tuned.wav')
        subprocess.run([str(args.renderer.resolve()), str(path.resolve()), str(target.resolve()), '--detect', source],
                       check=True, capture_output=True)
        y, sr = read(target); original, _ = read(path)
        assert y.shape == audio.shape and sr == rate and np.isfinite(y).all()
        pred = np.genfromtxt(str(target)+'.csv', delimiter=',', names=True)
        stable = pred['time_s'] > .2
        result = {'channels': y.shape[1], 'detector_source': source,
                  'voiced_coverage_after_200ms': float(np.mean(pred['input_hz'][stable] > 0))}
        if name == 'panned':
            result['max_pan_error'] = float(np.max(abs(y[:, 1]-.3*y[:, 0])))
            assert result['max_pan_error'] < 5e-5
        if name == 'opposite_left':
            result['max_polarity_error'] = float(np.max(abs(y[:, 0]+y[:, 1])))
            assert result['max_polarity_error'] < 3e-7
        if name == 'right_only':
            assert np.max(abs(y[:, 0])) == 0
            result['left_crosstalk_peak'] = 0
        if name == 'opposite_sum':
            assert result['voiced_coverage_after_200ms'] == 0
            result['max_dry_delay_error'] = float(np.max(abs(y[8:]-original[:-8])))
            assert result['max_dry_delay_error'] < 3e-7
        else:
            assert result['voiced_coverage_after_200ms'] > .99
            channel = 1 if name == 'right_only' else 0
            result['output_hz'] = float(pitch(y[:, channel], rate))
            result['output_error_cents'] = float(1200*np.log2(result['output_hz']/440))
            assert abs(result['output_error_cents']) < 15
        results[name] = result
    (args.directory/'metrics.json').write_text(json.dumps(results, indent=2)+'\n')
    print(json.dumps(results, indent=2))

if __name__ == '__main__': main()

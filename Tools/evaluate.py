#!/usr/bin/env python3
"""Reproducible synthetic singing/bleed tests. Needs Python + NumPy, no model downloads.

This is a stress fixture, not evidence of live-stage or commercial-product parity.
The renderer uses exactly the DSP linked into the VST3. F0 is scored at the output
timestamp without shifting the estimate to compensate for detection delay.
"""
import argparse
import json
from pathlib import Path
import subprocess
import wave
import numpy as np


def write_wav(path, audio, rate=48000):
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes((np.clip(audio, -1, 1) * 32767).astype("<i2").tobytes())


def read_wav(path):
    with wave.open(str(path), "rb") as f:
        raw = f.readframes(f.getnframes())
        if f.getsampwidth() == 3:
            b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
            v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
            v = (v ^ 0x800000) - 0x800000
            x = v / 8388608
        else:
            x = np.frombuffer(raw, "<i2") / 32768
        return x.reshape(-1, f.getnchannels()).mean(axis=1), f.getframerate()


def fixtures(folder):
    sr = 48000
    t = np.arange(sr * 8) / sr
    rng = np.random.default_rng(20260905)
    notes = np.select([t < 1.8, t < 3.2, t < 4.5, t < 5.9, t < 7.2], [69, 76, 64, 72, 79], 69)
    cents = 20 + 22 * np.sin(2 * np.pi * 5.2 * t)
    hz = 440 * 2 ** ((notes - 69 + cents / 100) / 12)
    voiced = (t > .3) & ~((t > 3.0) & (t < 3.2)) & ~((t > 5.7) & (t < 5.9)) & (t < 7.6)
    # Smooth voiced envelope without reading any future samples in the processor.
    env = np.convolve(voiced.astype(float), np.ones(240) / 240, mode="same")
    phase = np.cumsum(2 * np.pi * hz / sr)
    vocal = np.zeros_like(t)
    for harmonic in range(1, 13):
        frequency = hz * harmonic
        formants = .18 + np.exp(-.5 * ((frequency - 850) / 200) ** 2) + .5 * np.exp(-.5 * ((frequency - 2400) / 380) ** 2)
        vocal += formants / harmonic ** .9 * np.sin(harmonic * phase + .13 * harmonic)
    vocal *= env
    vocal *= .15 / np.sqrt(np.mean(vocal[voiced] ** 2))
    guitar = np.zeros_like(t)
    for h in range(1, 12):
        guitar += np.sin(2 * np.pi * 146.83 * h * t + h * .21) / h
    bass = .7 * np.sin(2 * np.pi * 82.41 * t)
    drum = rng.normal(size=len(t)) * np.exp(-((t * 2) % 1) * 25)
    bleed = guitar + bass + .65 * drum
    bleed /= np.sqrt(np.mean(bleed[voiced] ** 2))
    np.savez(folder / "truth.npz", hz=hz, voiced=voiced, notes=notes, time=t)
    paths = []
    for name, level in [("clean", None), ("bleed_10dB", 10), ("bleed_5dB", 5), ("bleed_0dB", 0)]:
        x = vocal + (0 if level is None else bleed * .15 * 10 ** (-level / 20))
        p = folder / (name + ".wav")
        write_wav(p, x)
        paths.append(p)
    return paths


def measure_output(audio, sr, truth):
    # Separate long-window NSDF estimator, with an expected-note search range.
    residual = []
    for centre in np.arange(.5, 7.5, .012):
        at = int(centre * sr)
        segment = truth["notes"][at - 2400:at + 2400]
        if len(segment) == 0 or not np.all(segment == segment[0]) or not np.all(truth["voiced"][at - 2400:at + 2400]):
            continue
        target = 440 * 2 ** ((truth["notes"][at] - 69) / 12)
        x = audio[at - 1200:at + 1200]
        lo, hi = int(sr / (target * 1.05)), int(sr / (target * .95))
        a = x[:1800]
        corr = np.array([2 * np.dot(a, x[k:k + 1800]) / (np.dot(a, a) + np.dot(x[k:k + 1800], x[k:k + 1800]) + 1e-15) for k in range(lo - 1, hi + 2)])
        peak = int(np.argmax(corr[1:-1])) + 1
        left, mid, right = corr[peak - 1:peak + 2]
        delta = np.clip(.5 * (left - right) / (left - 2 * mid + right + 1e-15), -.5, .5)
        measured = sr / (lo - 1 + peak + delta)
        residual.append(1200 * np.log2(measured / target))
    return {"output_abs_cents_median": float(np.median(np.abs(residual))),
            "output_abs_cents_p95": float(np.percentile(np.abs(residual), 95))}


def score(path, truth):
    a = np.genfromtxt(str(path) + ".csv", delimiter=",", names=True)
    indices = np.clip((a["time_s"] * 48000).astype(int), 0, len(truth["hz"]) - 1)
    reference = truth["hz"][indices]
    v = truth["voiced"][indices]
    got = a["input_hz"] > 0
    errors = 1200 * np.log2(np.maximum(a["input_hz"], 1) / reference)
    result = {
        "voiced_coverage": float(np.mean(got[v])),
        "rpa50_unshifted": float(np.mean((np.abs(errors[v]) < 50) & got[v])),
        "rpa25_unshifted": float(np.mean((np.abs(errors[v]) < 25) & got[v])),
        "false_voiced_silence": float(np.mean(got[~v])),
        "voiced_abs_cents_median": float(np.median(np.abs(errors[v & got]))),
        "voiced_abs_cents_p95": float(np.percentile(np.abs(errors[v & got]), 95)),
        "read_delay_ms_p50": float(np.median(a["read_delay_ms"][v])),
        "read_delay_ms_p99": float(np.percentile(a["read_delay_ms"][v], 99)),
    }
    audio, sr = read_wav(path)
    result.update(measure_output(audio, sr, truth))
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument("renderer", type=Path)
    p.add_argument("--directory", type=Path, default=Path("build-evaluation"))
    args = p.parse_args()
    folder = args.directory.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    files = fixtures(folder)
    truth = np.load(folder / "truth.npz")
    results = {}
    for audio in files:
        output = audio.with_name(audio.stem + "_tuned.wav")
        completed = subprocess.run([str(args.renderer.resolve()), str(audio), str(output)], check=True, capture_output=True, text=True)
        results[audio.stem] = score(output, truth)
        results[audio.stem]["renderer_timing"] = completed.stdout.strip().splitlines()[-1]
    text = json.dumps(results, indent=2)
    (folder / "metrics.json").write_text(text + "\n")
    print(text)


if __name__ == "__main__":
    main()

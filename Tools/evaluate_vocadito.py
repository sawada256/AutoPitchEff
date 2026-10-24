#!/usr/bin/env python3
"""Evaluate a predeclared higher-register vocadito subset, without training.

Dataset: Bittner et al., vocadito (2021), CC BY 4.0,
https://zenodo.org/records/5578807 . Download/extract separately.
Requires NumPy. Reuses the WAV and telemetry utilities in evaluate.py.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import numpy as np
from evaluate import read_wav, write_wav


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("renderer", type=Path)
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--directory", type=Path, default=Path("build-vocadito"))
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    rows = list(csv.DictReader((args.dataset / "vocadito_metadata.csv").open()))
    # Selection based on published metadata before observing processor results.
    rows = [r for r in rows if int(r["average_pitch"]) >= 60]
    results = {}
    for row in rows:
        name = "vocadito_" + row["track_id"]
        audio, sr = read_wav(args.dataset / "Audio" / (name + ".wav"))
        truth = np.loadtxt(args.dataset / "Annotations" / "F0" / (name + "_f0.csv"), delimiter=",")
        t = np.arange(len(audio)) / sr
        label = truth[np.minimum(np.searchsorted(truth[:, 0], t, side="right") - 1, len(truth) - 1), 1]
        rms = np.sqrt(np.mean(audio[label > 0] ** 2))
        rng = np.random.default_rng(940 + int(row["track_id"]))
        bleed = sum(np.sin(2 * np.pi * 146.83 * h * t + h * .21) / h for h in range(1, 12))
        bleed += .7 * np.sin(2 * np.pi * 82.41 * t) + .65 * rng.normal(size=len(t)) * np.exp(-((t * 2) % 1) * 25)
        bleed *= rms / np.sqrt(np.mean(bleed[label > 0] ** 2))
        for kind, mix in [("clean", audio), ("bleed_10dB", audio + bleed * 10 ** (-10 / 20))]:
            input_path = args.directory / (name + "_" + kind + ".wav")
            write_wav(input_path, mix, sr)
            output = input_path.with_name(input_path.stem + "_tuned.wav")
            subprocess.run([str(args.renderer.resolve()), str(input_path), str(output)], check=True, capture_output=True)
            pred = np.genfromtxt(str(output) + ".csv", delimiter=",", names=True)
            # Latest completed result at each reference time; never select a future result.
            index = np.clip(np.searchsorted(pred["time_s"], truth[:, 0], side="right") - 1, 0, len(pred) - 1)
            hz = pred["input_hz"][index]
            voiced = truth[:, 1] > 0
            in_range = voiced & (truth[:, 1] >= 180) & (truth[:, 1] <= 1200)
            error = np.abs(1200 * np.log2(np.maximum(hz, 1) / np.maximum(truth[:, 1], 1)))
            valid = hz > 0
            results[name + "_" + kind] = {
                "reference_frames_in_range": int(in_range.sum()),
                "voiced_frames_total": int(voiced.sum()),
                "rpa50_in_range_unshifted": float(np.mean((error[in_range] < 50) & valid[in_range])),
                "rpa25_in_range_unshifted": float(np.mean((error[in_range] < 25) & valid[in_range])),
                "coverage_in_range": float(np.mean(valid[in_range])),
                "false_voiced_unvoiced": float(np.mean(valid[~voiced])),
                "median_abs_cents_when_tracking_in_range": float(np.median(error[in_range & valid])),
            }
    aggregates = {}
    for kind in ["clean", "bleed_10dB"]:
        selected = [v for k, v in results.items() if k.endswith("_" + kind)]
        weights = [v["reference_frames_in_range"] for v in selected]
        aggregates[kind] = {metric: float(np.average([v[metric] for v in selected], weights=weights))
                            for metric in ["rpa50_in_range_unshifted", "rpa25_in_range_unshifted", "coverage_in_range"]}
    result = {"selection": "Published average MIDI pitch >= 60, 11 recordings; not a gender annotation.",
              "scoring": "All annotated voiced frames inside 180..1200 Hz, no delay compensation. Bleed is synthetic.",
              "aggregate": aggregates, "tracks": results}
    text = json.dumps(result, indent=2)
    (args.directory / "metrics.json").write_text(text + "\n")
    print(json.dumps(aggregates, indent=2))


if __name__ == "__main__":
    main()

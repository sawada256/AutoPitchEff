#!/usr/bin/env python3
"""Compare rendered PCM across two versions; requires only Python's stdlib."""
import argparse
import json
from pathlib import Path
import subprocess
import wave

p = argparse.ArgumentParser()
p.add_argument('before', type=Path)
p.add_argument('after', type=Path)
p.add_argument('inputs', type=Path, nargs='+')
p.add_argument('--directory', type=Path, default=Path('build-gui-audio-check'))
args = p.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
results = {}
for index, source in enumerate(args.inputs):
    outputs = []
    for label, renderer in [('before', args.before), ('after', args.after)]:
        output = args.directory / f'{index}_{source.stem}_{label}.wav'
        subprocess.run([str(renderer.resolve()), str(source.resolve()), str(output.resolve())],
                       check=True, capture_output=True)
        outputs.append(output)
    with wave.open(str(outputs[0]), 'rb') as a, wave.open(str(outputs[1]), 'rb') as b:
        same = a.getparams() == b.getparams() and a.readframes(a.getnframes()) == b.readframes(b.getnframes())
    results[str(source)] = {'pcm_identical': same}
(args.directory / 'metrics.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
if not all(row['pcm_identical'] for row in results.values()):
    raise SystemExit('Rendered PCM changed')

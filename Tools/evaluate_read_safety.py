#!/usr/bin/env python3
"""Compare the same read-head stress fixture against a Git tag and current DSP."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

PROBE = r'''
#include "PitchEngine.h"
#include <cmath>
#include <iostream>
int main() {
    for (double sr : {44100., 48000., 96000.}) {
        ape::PeriodShifter shifter; shifter.prepare(sr);
        int violations = 0; double minimum = 1e6;
        for (int i = 0; i < static_cast<int>(sr * 4); ++i) {
            const double ratio = 1 + .30 * std::sin(i * 2 * ape::pi / (sr * .007));
            shifter.process(.2f * static_cast<float>(std::sin(2 * ape::pi * 452 * i / sr)), ratio, sr / 452);
            const double age = shifter.minimumReadAgeSamples();
            minimum = std::min(minimum, age); if (age < 3 - 1e-8) ++violations;
        }
        std::cout << sr << ' ' << violations << ' ' << minimum << '\n';
    }
}
'''


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--before', default='v0.2.1')
    p.add_argument('--compiler', default='/usr/bin/clang++')
    p.add_argument('--directory', type=Path, default=Path('build-read-safety'))
    args = p.parse_args(); args.directory.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[1]; results = {}
    with tempfile.TemporaryDirectory(prefix='ape-read-safety-') as tmp:
        for name in ['before', 'after']:
            folder = Path(tmp)/name; folder.mkdir()
            for extension in ['h', 'cpp']:
                relative = f'Source/dsp/PitchEngine.{extension}'
                if name == 'before':
                    source = subprocess.run(['git', 'show', f'{args.before}:{relative}'], cwd=repo,
                                            check=True, capture_output=True, text=True).stdout
                else: source = (repo/relative).read_text()
                if extension == 'h' and 'minimumReadAgeSamples()' not in source:
                    # Add inspection only to the historical header, leaving its DSP unchanged.
                    source = source.replace('    float currentDelayMs() const noexcept {',
                        '    double minimumReadAgeSamples() const noexcept { return blend < 1 && otherDelay < delay ? otherDelay : delay; }\n    float currentDelayMs() const noexcept {')
                (folder/f'PitchEngine.{extension}').write_text(source)
            (folder/'probe.cpp').write_text(PROBE)
            executable = folder/'probe'
            subprocess.run([args.compiler, '-O2', '-std=c++17', str(folder/'probe.cpp'),
                            str(folder/'PitchEngine.cpp'), '-o', str(executable)], check=True, capture_output=True)
            output = subprocess.run([str(executable)], check=True, capture_output=True, text=True).stdout
            results[name] = {}
            for line in output.strip().splitlines():
                rate, count, minimum = line.split()
                results[name][rate] = {'unsafe_head_frames': int(count), 'minimum_read_age_samples': float(minimum)}
    (args.directory/'metrics.json').write_text(json.dumps(results, indent=2)+'\n')
    print(json.dumps(results, indent=2))
    assert all(row['unsafe_head_frames'] == 0 for row in results['after'].values())

if __name__ == '__main__': main()

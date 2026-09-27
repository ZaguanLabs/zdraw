#!/usr/bin/env python3
"""Compare rectangle batches with original triangle construction on captured game inputs."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile

from raster import trial

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'benchmarks/fixtures/cinder-rectangles.json.gz'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repetitions', type=int, default=5)
    parser.add_argument('--trials', type=int, default=3)
    parser.add_argument('--modes', nargs='+', choices=('headless', 'half', 'ascii', 'mono'),
                        default=['headless', 'half'])
    args = parser.parse_args()
    if not 1 <= args.repetitions <= 20 or not 1 <= args.trials <= 10:
        parser.error('repetitions must be 1..20; trials must be 1..10')
    fixture = json.loads(gzip.decompress(FIXTURE.read_bytes()))
    results = []
    shell, modules = ROOT / '.build/zsh/Src/zsh', ROOT / '.build/modules'
    for case in fixture['cases']:
        with tempfile.NamedTemporaryFile(mode='w', dir=ROOT / '.build', suffix='.rectangles') as source:
            source.write(f'{case["width"]} {case["height"]}\n')
            source.write(' '.join(str(color) for color, _ in fixture['palette']) + '\n')
            source.write(''.join(glyph for _, glyph in fixture['palette']) + '\n')
            for frame in case['frames']:
                source.write(' '.join(v for rect in frame['rectangles'] for v in rect) + '\n')
            source.flush()
            for mode in args.modes:
                hashes = set()
                for run in range(args.trials):
                    backends = ('triangles', 'rectangles') if run % 2 == 0 else ('rectangles', 'triangles')
                    for backend in backends:
                        result = trial(shell, modules, source.name, args.repetitions, backend, mode,
                                       case['width'], case['height'], script='raster-rectangles.zsh',
                                       phase_names=('split_ms', 'construct_ms', 'clear_ms', 'raster_ms',
                                                    'pack_ms', 'present_ms', 'total_ms'))
                        result.update(width=case['width'], height=case['height'], ray_step=case['ray_step'],
                                      trial=run, rectangles_min=min(len(f['rectangles']) for f in case['frames']),
                                      rectangles_max=max(len(f['rectangles']) for f in case['frames']))
                        results.append(result)
                        hashes.add(result['output_sha256'])
                if len(hashes) != 1:
                    raise RuntimeError(f'Terminal output differs: {case["width"]}, {mode}')
    print(json.dumps(dict(platform=platform.platform(),
                          shell=subprocess.check_output([shell, '--version'], text=True).strip(),
                          locale=os.environ.get('ZDRAW_TEST_LOCALE', 'C.UTF-8'),
                          fixture_sha256=hashlib.sha256(FIXTURE.read_bytes()).hexdigest(),
                          source_sha256={name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                                         for name in ('Src/Modules/zdraw_raster.h', 'benchmarks/raster-rectangles.zsh')},
                          note='Captured Cinder Relay rectangle inputs; includes shell construction/submission. Projection, ray traversal, simulation and HUD excluded. Drained PTY is not emulator paint latency.',
                          results=results), indent=2))


if __name__ == '__main__':
    main()

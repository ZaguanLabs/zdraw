#!/usr/bin/env python3
"""Measure finished RGB painting presentation; importing geometry is separately timed."""
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
FIXTURE = ROOT/'benchmarks/fixtures/alpine-rgb.json.gz'


def inputs(fixture, directory):
    palette, pixels = fixture['palette'], fixture['pixels']
    (directory/'frame').write_text(' '.join(palette)+'\n'+' '.join(map(str, pixels))+'\n')
    for t in range(9):
        ox, oy = t % 3*250, t//3*132
        batch = []
        with (directory/f'tile{t}').open('w') as stream:
            for y in range(132):
                x = 0
                while x < 250:
                    start = x
                    color = pixels[(oy+y)*750+ox+x]
                    while x < 250 and pixels[(oy+y)*750+ox+x] == color:
                        x += 1
                    rgb = '#'+palette[color]
                    vertices = [(start, y), (x, y), (x, y+1), (start, y+1)]
                    for indices in ((0, 1, 2), (0, 2, 3)):
                        for i in indices:
                            batch += [str(vertices[i][0]), str(vertices[i][1]), '1', rgb]
                        batch += ['0']
                    # Bound parsing and bbox work even for long horizontal runs.
                    if len(batch) >= 26*256:
                        stream.write(' '.join(batch)+'\n')
                        batch = []
            if batch:
                stream.write(' '.join(batch)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trials', type=int, default=3)
    args = parser.parse_args()
    if not 1 <= args.trials <= 10:
        parser.error('trials must be 1..10')
    fixture = json.loads(gzip.decompress(FIXTURE.read_bytes()))
    results = []
    with tempfile.TemporaryDirectory(dir=ROOT/'.build') as tmp:
        directory = Path(tmp)
        inputs(fixture, directory)
        terminfo = directory/'terminfo'
        subprocess.run(['tic', '-x', '-o', str(terminfo), str(ROOT/'tests/truecolor.terminfo')], check=True, capture_output=True)
        os.environ['TERMINFO'] = str(terminfo)
        for run in range(args.trials):
            for backend in (('reference', 'native') if run % 2 == 0 else ('native', 'reference')):
                result = trial(ROOT/'.build/zsh/Src/zsh', ROOT/'.build/modules', directory, 1,
                               backend, 'half', 750, 396, script='raster-rgb.zsh',
                               terminal_name='zdraw-test-rgb',
                               phase_names=('setup_ms', 'pack_ms', 'present_ms', 'frame_ms'))
                result['trial'] = run
                results.append(result)
        if len({r['output_sha256'] for r in results}) != 1:
            raise RuntimeError('Terminal streams differ between backends or trials')
    print(json.dumps(dict(platform=platform.platform(),
                          shell=subprocess.check_output([ROOT/'.build/zsh/Src/zsh', '--version'], text=True).strip(),
                          fixture_sha256=hashlib.sha256(FIXTURE.read_bytes()).hexdigest(),
                          source_sha256={name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
                                         for name in ('Src/Modules/zdraw_raster.h', 'benchmarks/raster-rgb.zsh', 'benchmarks/alpine-view-reference.zsh')},
                          ppm_sha256=fixture['ppm_sha256'],
                          note='Fresh session per trial. Identical PTY output required. Setup imports final geometry pixels and is excluded from frame timing. Application build/effects/quantization and emulator painting excluded.',
                          results=results), indent=2))


if __name__ == '__main__':
    main()

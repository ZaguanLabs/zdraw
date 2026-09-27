#!/usr/bin/env python3
"""Explicitly capture Alpine Vigil geometry and final renderer pixels; no cache writes."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'benchmarks/fixtures/alpine-rgb.json.gz')
    args = parser.parse_args()
    source = args.source.resolve()
    # Only this explicit capture command sources the external application.
    script = r'''
        emulate -R zsh
        module_path=("$1" $module_path)
        scene_root=$2
        zmodload zdraw && zmodload zsh/mathfunc || exit 1
        for lib in paint scene details painting brushwork; do
            source "$scene_root/lib/$lib.zsh"
        done
        painting-enable
        functions[capture-finish]=$functions[paint-finish]
        paint-finish() {
            paint-flush || return
            print -r -- "${(j: :)rgb}"
            local t
            for t in 3 11 22; do
                print -r -- "$tile_width[t] $tile_height[t] $tile_x[t] $tile_y[t]"
                print -r -- "$tile_commands[t]"
            done
            capture-finish || return
            print -r -- "$art_w $art_h $triangles"
            print -r -- "${(j: :)rgb}"
            print -r -- "${(j: :)pixels}"
        }
        scene-build || exit 1
    '''
    result = subprocess.run([str(ROOT/'.build/zsh/Src/zsh'), '-dfc', script, 'capture',
                             str(ROOT/'.build/modules'), str(source)], text=True,
                            capture_output=True, timeout=180)
    if result.returncode:
        raise RuntimeError(result.stderr or f"capture exited {result.returncode}")
    lines = iter(result.stdout.splitlines())
    pigments = next(lines).split()
    tiles = []
    for _ in range(3):
        w, h, x, y = map(int, next(lines).split())
        words = next(lines).split()
        assert len(words) % 10 == 0
        tiles.append(dict(width=w, height=h, x=x, y=y,
                          triangles=[words[i:i+10] for i in range(0, len(words), 10)]))
    w, h, triangles = map(int, next(lines).split())
    palette = next(lines).split()
    words = next(lines).split()
    ids = list(map(int, words[::2]))
    assert len(ids) == w*h and len(words) == 2*w*h
    assert all(0 <= i < len(palette) for i in ids)
    ppm = f'P6\n{w} {h}\n255\n'.encode()+b''.join(bytes.fromhex(palette[i]) for i in ids)
    fixture = dict(format='alpine-rgb-1', description='Fresh native-geometry Alpine Vigil painting, display-encoded final RGB after application effects and pair quantization.',
                   source_sha256={p.relative_to(source).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in sorted((source/'lib').glob('*.zsh'))},
                   shell=subprocess.check_output([ROOT/'.build/zsh/Src/zsh', '--version'], text=True).strip(),
                   capture_base='zdraw v0.1.2 indexed API; experimental working build',
                   width=w, height=h, triangles=triangles, pigments=pigments, tiles=tiles,
                   palette=palette, pixels=ids, ppm_sha256=hashlib.sha256(ppm).hexdigest())
    args.output.write_bytes(gzip.compress(json.dumps(fixture, separators=(',', ':')).encode(), mtime=0))
    print(json.dumps({k: fixture[k] for k in ('width', 'height', 'triangles', 'ppm_sha256')}, indent=2))


if __name__ == '__main__':
    main()

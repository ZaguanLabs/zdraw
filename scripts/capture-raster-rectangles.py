#!/usr/bin/env python3
"""Opt-in capture from a supplied Cinder Relay tree; never used by normal tests."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('levels/relay.zsh', 'lib/engine.zsh', 'lib/art.zsh', 'lib/render.zsh', 'game.zsh')
CAPTURE = r'''
emulate -R zsh
module_path=("$2" "$3/Src/Modules")
zmodload zsh/mathfunc && zmodload zdraw || exit 1
for part in levels/relay lib/engine lib/art lib/render; do source "$1/$part.zsh" || exit 2; done
print -r -- "${(j: :)ink}"
print -r -- "${(j::)density}"
typeset -i frame pose
typeset -a data
for width height in 64 28 80 36 100 52; do
  for ray_step in 2 1; do
    game-reset
    for ((frame=0;frame<12;frame++)); do
      ((world_time=frame,player_x=8.5,player_y=16.5,player_z=0,
        player_angle=-1.570796327+0.30*sin(world_time*0.4)))
      render-scene || exit 3
      zdraw raster read relay data || exit 4
      print -r -- "$width $height $ray_step sweep-$frame"
      print -r -- "${(j: :)triangle_batch}"
      print -r -- "${(j: :)data}"
    done
    for pose in 1 2 3 4 5; do
      game-reset
      case $pose in
        1) player_x=7.5 player_y=20.22 player_z=0 player_angle=-1.57 door_open[1]=0 ;;
        2) player_x=7.5 player_y=20.22 player_z=0 player_angle=-1.57 door_open[1]=0.5 ;;
        3) player_x=16.2 player_y=14.5 player_z=0 player_angle=0 door_open[2]=1 ;;
        4) player_x=25.5 player_y=19.5 player_z=0.36 player_angle=-0.7 ;;
        5) player_x=29.5 player_y=4.5 player_z=0.36 player_angle=-1.0 ;;
      esac
      render-scene || exit 5
      zdraw raster read relay data || exit 6
      print -r -- "$width $height $ray_step pose-$pose"
      print -r -- "${(j: :)triangle_batch}"
      print -r -- "${(j: :)data}"
    done
  done
done
zdraw end
'''


def triangles(rect):
    x0, y0, x1, y1, qt, qb, material = rect
    return [x0, y0, qt, x1, y0, qt, x1, y1, qb, material,
            x0, y0, qt, x1, y1, qb, x0, y1, qb, material]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve()
    shell = ROOT / '.build/zsh/Src/zsh'
    run = subprocess.run([shell, '-dfc', CAPTURE, 'capture', source,
                          ROOT / '.build/modules', ROOT / '.build/zsh'],
                         capture_output=True, text=True, timeout=120,
                         env={'LC_ALL': 'C', 'PATH': '/usr/bin:/bin'})
    if run.returncode:
        raise SystemExit(run.stderr)
    lines = run.stdout.splitlines()
    cases = {}
    for index in range(2, len(lines), 3):
        width, height, step, label = lines[index].split()
        key = (int(width), int(height), int(step))
        case = cases.setdefault(key, dict(width=key[0], height=key[1], ray_step=key[2], frames=[]))
        words = lines[index + 1].split()
        assert len(words) % 20 == 0
        rectangles = []
        for start in range(0, len(words), 20):
            pair = words[start:start + 20]
            rect = [pair[n] for n in (0, 1, 6, 7, 2, 8, 9)]
            assert triangles(rect) == pair, 'Source no longer uses the expected rectangle expansion'
            rectangles.append(rect)
        readback = lines[index + 2].split()
        case['frames'].append(dict(label=label, rectangles=rectangles,
                                   materials=list(map(int, readback[::2])),
                                   depth=list(map(float, readback[1::2]))))
    fixture = dict(format='zdraw-cinder-rectangles-1',
                   provenance=dict(application='Cinder Relay archive, 2026-09-26',
                                   source_sha256={name: hashlib.sha256((source / name).read_bytes()).hexdigest()
                                                  for name in SOURCES},
                                   shell=subprocess.check_output([shell, '--version'], text=True).strip(),
                                   note='Captured original triangle batches and readback; AI inactive. Sweep samples frame indices 0,60,...,660; five additional poses from tests/render.zsh, with actors and weapon retained.'),
                   palette=list(zip(map(int, lines[0].split()), lines[1])),
                   cases=list(cases.values()))
    args.output.write_bytes(gzip.compress(json.dumps(fixture, separators=(',', ':')).encode(), mtime=0))
    print(f'Captured {sum(len(c["frames"]) for c in cases.values())} frames into {args.output}')


if __name__ == '__main__':
    main()

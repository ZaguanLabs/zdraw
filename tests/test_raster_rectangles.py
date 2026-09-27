"""Rectangle submission versus the original two-triangle contract and captures."""
import gzip
import json
import math
import random
import shlex
import subprocess
import unittest

import test_features

ROOT, ZSH = test_features.ROOT, test_features.ZSH


def expand(rect):
    x0, y0, x1, y1, qt, qb, material = rect
    return [x0, y0, qt, x1, y0, qt, x1, y1, qb, material,
            x0, y0, qt, x1, y1, qb, x0, y1, qb, material]


def compare_frames(width, height, frames):
    script = ['zmodload zdraw || exit 1']
    for name in ('rect', 'tri'):
        script.append(f'zdraw raster create {name} {width} {height} ' +
                      ' '.join(f'{i} "#"' for i in range(32)) + ' || exit 2')
    for rectangles in frames:
        for name, operation, words_per_rect in (('rect', 'rectangles', 7), ('tri', 'triangles', 20)):
            script.append(f'zdraw raster clear {name} 0 1 || exit 3')
            for start in range(0, len(rectangles), 2048):
                batch = rectangles[start:start + 2048]
                words = [v for rect in batch for v in (rect if words_per_rect == 7 else expand(rect))]
                script.append(f'zdraw raster {operation} {name} ' +
                              ' '.join(shlex.quote(str(v)) for v in words) + ' || exit 4')
            script += [f'zdraw raster read {name} data || exit 5', 'print -r -- "${(j: :)data}"']
    script.append('zdraw end || exit 6')
    # stdin avoids the OS argument-length limit for captured batches.
    result = subprocess.run([ZSH, '-dfs', '--', str(ROOT / '.build/modules')],
                            input='module_path=("$1")\n' + '\n'.join(script),
                            capture_output=True, text=True, timeout=60, start_new_session=True)
    if result.returncode:
        raise AssertionError(result.stderr)
    lines = result.stdout.splitlines()
    assert len(lines) == len(frames) * 2
    for i in range(0, len(lines), 2):
        if lines[i] != lines[i + 1]:
            raise AssertionError(f'Rectangle/triangle readback differs in frame {i//2}')
    return [(list(map(int, line.split()[::2])), list(map(float, line.split()[1::2])))
            for line in lines[::2]]


class RasterRectangleTests(unittest.TestCase):
    def test_coverage_and_vertical_depth(self):
        actual = compare_frames(8, 4, [[[0, 0, 8, 4, .2, 1.2, 3]]])[0]
        self.assertEqual(actual[0], [3] * 32)
        for i, depth in enumerate(actual[1]):
            self.assertAlmostEqual(depth, .2 + (i//8 + .5)/4, delta=1e-12)

    def test_exact_expansion_edges_ties_and_randomized_batches(self):
        frames = [
            [[.5, .5, 7.5, 5.5, .3, 1.1, 2], [.5, .5, 7.5, 5.5, .3, 1.1, 3]],
            [[-32768, -32768, 32768, 32768, .000001, 1000000, 4]],
            [[0, 0, 0, 8, 1, 2, 2], [0, 0, 8, 0, 1, 2, 3]],
            [[0, 0, .000001, 1, 1, 2, 2], [-10, -10, -2, -2, 1, 2, 3]],
            [[0, 0, 6, 8, 1, 2, 2], [6, 0, 12, 8, 1, 2, 3], [1, 1, 11, 7, 2, 1, 4]],
        ]
        rng = random.Random(20260927)
        for _ in range(30):
            frame = []
            for _ in range(24):
                x0, x1 = sorted(round(rng.uniform(-8, 24), 7) for _ in range(2))
                y0, y1 = sorted(round(rng.uniform(-8, 20), 7) for _ in range(2))
                frame.append([x0, y0, x1, y1, rng.uniform(.01, 5), rng.uniform(.01, 5), rng.randrange(32)])
            frames.append(frame)
        compare_frames(16, 12, frames)

    def test_cinder_relay_captured_frames(self):
        fixture = json.loads(gzip.decompress((ROOT / 'benchmarks/fixtures/cinder-rectangles.json.gz').read_bytes()))
        for case in fixture['cases']:
            with self.subTest(width=case['width'], height=case['height'], ray_step=case['ray_step']):
                actual = compare_frames(case['width'], case['height'],
                                        [f['rectangles'] for f in case['frames']])
                for frame, (materials, depths) in zip(case['frames'], actual):
                    self.assertEqual(materials, frame['materials'])
                    self.assertTrue(all(math.isclose(a, b, abs_tol=2e-6, rel_tol=2e-6)
                                        for a, b in zip(depths, frame['depth'])), frame['label'])

    def test_atomic_bounds_mixed_primitives_and_lifecycle(self):
        output = test_features.FeatureTests().run_shell(r'''
            zmodload zdraw || exit 1
            fail() { print -ru2 -- "FAIL $*"; exit 1; }
            check() { "$@" || fail "$*"; }
            reject() { "$@" 2>/dev/null && fail "$*"; return 0; }
            check zdraw raster create s 4 3 0 ' ' 1 '#'
            check zdraw raster triangles s 0 0 1 4 0 1 0 3 1 1
            check zdraw raster rectangles s +0 0 4.0 3 2e0 .5 1
            check zdraw raster read s before
            for bad in NaN inf -inf 1e301 32769 1x '$((1))' ' 1' '' $'1\0x'; do
                reject zdraw raster rectangles s 0 0 4 3 3 3 0 "$bad" 0 4 3 1 1 1
            done
            for bad in 0 -1 1000001 NaN; do
                reject zdraw raster rectangles s 0 0 4 3 "$bad" 1 0
                reject zdraw raster rectangles s 0 0 4 3 1 "$bad" 0
            done
            reject zdraw raster rectangles s 4 0 0 3 1 1 0
            reject zdraw raster rectangles s 0 3 4 0 1 1 0
            reject zdraw raster rectangles s 0 0 4 3 1 1 2
            reject zdraw raster rectangles s 0
            check zdraw raster rectangles s
            check zdraw raster rectangles s 0 0 0 3 1 1 0
            check zdraw raster read s after
            [[ ${(j: :)before} == ${(j: :)after} ]] || fail atomicity
            check zdraw raster info s info
            (( info[rectangle_limit] == 2048 )) || fail rectangle_limit
            typeset -a batch
            repeat 2048; do batch+=(0 0 0 0 1 1 0); done
            check zdraw raster rectangles s "${batch[@]}"
            batch+=(0 0 0 0 1 1 0)
            reject zdraw raster rectangles s "${batch[@]}"
            check zdraw raster resize s 512 128
            batch=()
            repeat 128; do batch+=(0 0 512 128 1 1 0); done
            check zdraw raster rectangles s "${batch[@]}"
            check zdraw raster clear s 0
            batch+=(0 0 512 128 1 1 0)
            reject zdraw raster rectangles s "${batch[@]}"
            check zdraw raster read s after
            (( ! ${after[(Ie)1]} )) || fail work_limit_changed_depth
            check zdraw end
            check zdraw resourceinfo info
            (( info[raster_bytes] == 0 )) || fail end
            print PASS
        ''')
        self.assertEqual(output, 'PASS\n')

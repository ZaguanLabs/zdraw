"""Experimental raster: independent coverage/depth oracle and terminal contract."""
import gzip
import json
import math
import random
import shlex
import subprocess
import tempfile
import unittest

from test_drawing import drawing_session
import test_features

ROOT, ZSH = test_features.ROOT, test_features.ZSH


def reference(width, height, triangles, background=None):
    """Direct per-pixel barycentrics; no native traversal or incremental steps."""
    pixels = list(background or [0] * (width * height))
    depth = [0.0] * len(pixels)
    for triangle in triangles:
        ax, ay, aq, bx, by, bq, cx, cy, cq, material = triangle
        area = (bx-ax)*(cy-ay) - (by-ay)*(cx-ax)
        if abs(area) <= 1e-6:
            continue
        for y in range(height):
            for x in range(width):
                px, py = x + 0.5, y + 0.5
                a = ((bx-px)*(cy-py) - (by-py)*(cx-px)) / area
                b = ((cx-px)*(ay-py) - (cy-py)*(ax-px)) / area
                c = 1-a-b
                q = a*aq + b*bq + c*cq
                index = y*width+x
                if min(a, b, c) >= -1e-6 and q > depth[index]:
                    pixels[index], depth[index] = material, q
    return pixels, depth


def native_frames(width, height, frames, background=(0, 0)):
    script = ['zmodload zdraw || exit 1',
              f'zdraw raster create s {width} {height} ' +
              ' '.join(f'{i} "#"' for i in range(16)) + ' || exit 2']
    for triangles in frames:
        script += [f'zdraw raster clear s {background[0]} {background[1]} || exit 3',
                   'zdraw raster triangles s ' + ' '.join(
                       shlex.quote(str(value)) for triangle in triangles for value in triangle)
                   + ' || exit 4',
                   'zdraw raster read s result || exit 5',
                   'print -r -- "${(j: :)result}"']
    result = subprocess.run([ZSH, '-dfc', 'module_path=("$1");\n' + '\n'.join(script),
                             'raster-test', str(ROOT / '.build/modules')],
                            capture_output=True, text=True, timeout=30, start_new_session=True)
    if result.returncode:
        raise AssertionError(result.stderr)
    return [(list(map(int, line.split()[::2])), list(map(float, line.split()[1::2])))
            for line in result.stdout.splitlines()]


class RasterTests(unittest.TestCase):
    def test_crossing_clipping_winding_and_shared_edges(self):
        cases = [
            [[0, 0, 0.2, 12, 0, 1.2, 0, 12, 0.2, 1],
             [0, 0, 1.2, 12, 0, 0.2, 0, 12, 1.2, 2]],
            [[0, 0, 1, 8, 0, 1, 0, 8, 1, 1],
             [8, 0, 1, 8, 8, 1, 0, 8, 1, 2]],
            [[-32768, -32768, 1, 32768, -32768, 1, 0, 32768, 1, 3]],
            [[-10, -10, 1, -5, -10, 1, -10, -5, 1, 1]],
            [[1, 1, 1, 2, 2, 1, 3, 3, 1, 1]],
            [[0, 0, 1, 0, 8, 1, 8, 0, 1, 1]],
            # Same geometry/depth twice: first submitted material wins.
            [[0, 0, 0.7, 8, 0, 0.3, 0, 8, 0.8, 1],
             [0, 0, 0.7, 8, 0, 0.3, 0, 8, 0.8, 2]],
        ]
        for triangles, actual in zip(cases, native_frames(12, 12, cases)):
            expected = reference(12, 12, triangles)
            self.assertEqual(actual[0], expected[0])
            for a, b in zip(actual[1], expected[1]):
                self.assertAlmostEqual(a, b, delta=1e-10)

    def test_randomized_reference(self):
        rng = random.Random(20260914)
        frames = [[[round(rng.uniform(-20, 40), 5) if j % 3 != 2 else
                    round(rng.uniform(0.01, 6), 5) for j in range(9)] + [rng.randrange(1, 16)]
                   for _ in range(12)] for _ in range(24)]
        for triangles, actual in zip(frames, native_frames(24, 16, frames)):
            expected = reference(24, 16, triangles)
            self.assertEqual(actual[0], expected[0])
            for a, b in zip(actual[1], expected[1]):
                self.assertTrue(math.isclose(a, b, abs_tol=1e-9, rel_tol=1e-9), (a, b))

    def test_captured_camera_sweep(self):
        fixture = json.loads(gzip.decompress((ROOT / 'benchmarks/fixtures/raster-sweep.json.gz').read_bytes()))
        for size in fixture['sizes']:
            width, height = size['width'], size['height']
            frames = [frame['triangles'] for frame in size['frames']]
            actual_frames = native_frames(width, height, frames, (1, 2))
            for frame, actual in zip(size['frames'], actual_frames):
                self.assertEqual(actual[0], frame['pixels'])
                for a, b in zip(actual[1], frame['depth']):
                    self.assertTrue(math.isclose(a, b, abs_tol=2e-6, rel_tol=2e-6), (a, b))

    def test_atomic_validation_lifecycle_and_bounds(self):
        output = test_features.FeatureTests().run_shell(r'''
            zmodload zdraw || exit 1
            fail() { print -ru2 -- "FAIL $*"; exit 1; }
            check() { "$@" || fail "$*"; }
            reject() { "$@" 2>/dev/null && fail "$*"; return 0; }
            check zdraw raster create s 4 3 0 ' ' 1 '#'
            check zdraw raster triangles s +0 0 1e0 4.0 0 .5 0 3 1 1
            check zdraw raster read s before
            for bad in NaN inf -inf 1e301 32769 1x '$((1))' ' 1' '' $'1\0x'; do
                reject zdraw raster triangles s 0 0 1 4 0 1 0 3 1 0 "$bad" 0 1 4 0 1 0 3 1 1
            done
            reject zdraw raster triangles s 0 0 0 4 0 1 0 3 1 1
            reject zdraw raster triangles s 0 0 1 4 0 1 0 3 1 2
            reject zdraw raster triangles s 0
            reject zdraw raster clear s 1 2
            reject zdraw raster resize s 0 1
            reject zdraw raster resize s 512 512
            reject zdraw raster create s 1 1 0 ' '
            reject zdraw raster create 'bad[name]' 1 1 0 ' '
            reject zdraw raster create bad 1 1 256 ' '
            reject zdraw raster create bad 1 1 0 $'\e'
            check zdraw raster read s after
            [[ $before == $after ]] || fail atomicity
            typeset scalar=sentinel
            typeset -ar frozen=(sentinel)
            typeset -A assoc=(sentinel yes)
            for target in scalar frozen assoc path 'a[x]' 'x;evil=1'; do
                reject zdraw raster read s "$target"
            done
            [[ $scalar == sentinel && $frozen == sentinel && $assoc[sentinel] == yes ]] || fail targets
            check zdraw raster info s info
            (( info[width] == 4 && info[height] == 3 )) || fail dimensions
            reject zdraw raster info s scalar
            check zdraw raster resize s 2 2
            check zdraw raster read s after
            [[ ${(j: :)after} == '0 0 0 0 0 0 0 0' ]] || fail resize
            check zdraw raster clear s 1
            check zdraw raster triangles s
            check zdraw raster read s after
            [[ ${(j: :)after} == '1 0 1 0 1 0 1 0' ]] || fail clear
            check zdraw raster free s
            reject zdraw raster read s after
            check zdraw raster create s 512 128 0 ' '
            typeset -a batch
            repeat 257; do batch+=(0 0 1 512 0 1 0 128 1 0); done
            reject zdraw raster triangles s "${batch[@]}"
            check zdraw raster read s after
            (( ! ${after[(Ie)1]} )) || fail work_limit_changed_depth
            # Headless commands leave pending input for its shell owner.
            () {
                check zdraw raster info s info
                read -r line || fail input
                [[ $line == pending ]] || fail consumed_input
            } <<<pending
            check zdraw raster info s info
            typeset -i n=0
            while zdraw raster create "s$n" 512 128 0 ' ' 2>/dev/null; do
                (( ++n < 32 )) || fail budget
            done
            (( n > 0 )) || fail no_capacity
            check zdraw resourceinfo info
            (( info[raster_bytes] <= info[raster_byte_limit] )) || fail accounting
            check zdraw end
            check zdraw resourceinfo info
            (( info[raster_bytes] == 0 && info[raster_surfaces] == 0 )) || fail end
            check zdraw raster create s 1 1 0 ' '
            check zmodload -u zdraw
            check zmodload zdraw
            check zdraw resourceinfo info
            (( info[raster_bytes] == 0 )) || fail unload
            print PASS
        ''')
        self.assertEqual(output, 'PASS\n')

    def test_terminal_packing(self):
        drawing_session(self, 'wide', fixture='raster.zsh', marker=b'RASTER PASS')
        drawing_session(self, 'mono', env={'TERM': 'vt100'},
                        fixture='raster.zsh', marker=b'RASTER PASS')

    def test_optional_builds(self):
        original = (ROOT / 'Src/Modules/zdraw.c').read_text()
        for mode, definitions in (
            ('narrow', '#undef HAVE_WADD_WCHNSTR'),
            ('unavailable', '#undef HAVE_WADD_WCHNSTR\n#undef HAVE_WADDCHNSTR'),
            ('pair_failure', '#define init_pair(p, f, b) (ERR)'),
        ):
            source = original.replace('#include <stdio.h>', '#include <stdio.h>\n' + definitions, 1)
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(dir=ROOT / '.build') as tmp:
                modules = test_features.FeatureTests().variant(tmp, source)
                drawing_session(self, mode, modules, fixture='raster.zsh', marker=b'RASTER PASS')


if __name__ == '__main__':
    unittest.main(verbosity=2)

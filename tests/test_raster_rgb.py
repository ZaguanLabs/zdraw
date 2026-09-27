"""RGB/depth independence, independent area-box oracle, and real terminal packing."""
from fractions import Fraction
import random
import gzip
import json
import shlex
import subprocess
import tempfile
from pathlib import Path
import unittest

import test_features
from test_drawing import drawing_session

ROOT = test_features.ROOT


def run(script):
    return test_features.FeatureTests().run_shell('''
        zmodload zdraw || exit 1
        fail() { print -ru2 -- "FAIL $*"; exit 1; }
        check() { "$@" || fail "$*"; }
        reject() { "$@" 2>/dev/null && fail "$*"; return 0; }
    ''' + script)


def box(values, width, height, ow, oh):
    result = []
    for y in range(oh):
        for x in range(ow):
            x0, x1 = Fraction(x*width, ow), Fraction((x+1)*width, ow)
            y0, y1 = Fraction(y*height, oh), Fraction((y+1)*height, oh)
            color = 0
            for shift in (0, 8, 16):
                total = sum(((values[sy*width+sx] >> shift) & 255) *
                            max(0, min(x1, sx+1)-max(x0, sx)) *
                            max(0, min(y1, sy+1)-max(y0, sy))
                            for sy in range(height) for sx in range(width))
                color |= int(total/((x1-x0)*(y1-y0)) + Fraction(1, 2)) << shift
            result.append(f'#{color:06x}')
    return result


class RasterRGBTests(unittest.TestCase):
    def test_interpolation_depth_and_box_reference(self):
        rng = random.Random(72)
        width, height = 9, 7
        commands = ["check zdraw raster create-rgb s 9 7 '#000000' '.' '#abcdef' '#'"]
        colors = [0]*(width*height)
        depth = [0.]*(width*height)
        for _ in range(20):
            vertices = [(rng.uniform(-3, 12), rng.uniform(-3, 10), rng.uniform(.1, 5), rng.randrange(1 << 24)) for _ in range(3)]
            args = [str(v) if i < 3 else f'#{v:06x}' for vertex in vertices for i, v in enumerate(vertex)]
            commands.append('check zdraw raster triangles-rgb s ' + shlex.join(args+['1']))
            (ax, ay, aq, ac), (bx, by, bq, bc), (cx, cy, cq, cc) = vertices
            area = (bx-ax)*(cy-ay)-(by-ay)*(cx-ax)
            for y in range(height):
                for x in range(width):
                    px, py = x+.5, y+.5
                    a = ((bx-px)*(cy-py)-(by-py)*(cx-px))/area
                    b = ((cx-px)*(ay-py)-(cy-py)*(ax-px))/area
                    c = 1-a-b
                    q = a*aq+b*bq+c*cq
                    i = y*width+x
                    if min(a, b, c) >= -1e-6 and q > depth[i]:
                        depth[i] = q
                        colors[i] = sum(max(0, min(255, int(a*((ac>>s)&255)+b*((bc>>s)&255)+c*((cc>>s)&255)+.5))) << s for s in (0, 8, 16))
        commands += ['check zdraw raster read-rgb s data', 'print -r -- "$data"']
        for ow, oh in ((9, 7), (3, 1), (4, 3), (1, 1)):
            commands += [f'check zdraw raster resolve s out {ow} {oh}', 'check zdraw raster read-rgb out data', 'print -r -- "$data"', 'check zdraw raster free out']
        lines = run('\n'.join(commands)).splitlines()
        data = lines[0].split()
        self.assertEqual(data[::2], [f'#{v:06x}' for v in colors])
        for a, b in zip(map(float, data[1::2]), depth):
            self.assertAlmostEqual(a, b, delta=1e-10)
        for line, (ow, oh) in zip(lines[1:], ((9, 7), (3, 1), (4, 3), (1, 1))):
            self.assertEqual(line.split()[::2], box(colors, width, height, ow, oh))
            self.assertEqual(set(line.split()[1::2]), {'0'})

    def test_exact_rounding_and_owned_budget(self):
        self.assertIn('PASS', run(r"""
            check zdraw raster create-rgb s 2 2 '#000000' '.' '#000001' '#'
            check zdraw raster rectangles s 0 0 1 2 1 1 1
            check zdraw raster resolve s tiny 1 1
            check zdraw raster read-rgb tiny data
            [[ $data == '#000001 0' ]] || fail half_up
            check zdraw raster free s
            check zdraw raster free tiny
            check zdraw raster create-rgb s 512 128 '#ffffff' '#'
            typeset -a batch data
            repeat 257; do batch+=(0 0 1 '#000000' 512 0 1 '#000000' 0 128 1 '#000000' 0); done
            reject zdraw raster triangles-rgb s "${batch[@]}"
            check zdraw raster read-rgb s data
            (( ! ${data[(Ie)1]} )) || fail work_atomicity
            typeset -i n=0
            while zdraw raster create-rgb "s$n" 512 128 '#112233' '.' 2>/dev/null; do
                (( ++n < 32 )) || fail budget
            done
            check zdraw raster info s before
            reject zdraw raster resolve s extra 512 128
            check zdraw raster info s after
            [[ ${(kv)before} == ${(kv)after} ]] || fail failed_resolve_leaked
            check zdraw end
            check zdraw resourceinfo info
            (( !info[raster_bytes] )) || fail end
            print PASS
        """))

    def test_captured_geometry_keeps_depth_independent(self):
        fixture = json.loads(gzip.decompress((ROOT/'benchmarks/fixtures/alpine-rgb.json.gz').read_bytes()))
        for tile in fixture['tiles']:
            commands = [f"check zdraw raster create old {tile['width']} {tile['height']} " +
                        ' '.join(f"{i} '.'" for i in range(32)),
                        f"check zdraw raster create-rgb new {tile['width']} {tile['height']} " +
                        ' '.join(f"'#{v}' '.'" for v in fixture['pigments'][:32]),
                        'check zdraw raster clear old 5', 'check zdraw raster clear new 5']
            for start in range(0, len(tile['triangles']), 128):
                original, colored = [], []
                for triangle in tile['triangles'][start:start+128]:
                    original.extend(triangle)
                    for j in (0, 3, 6):
                        q = float(triangle[j+2])
                        encoded = q*256
                        pigment = int(encoded+.5) % 256
                        light = 1+(encoded-int(encoded+.5))*.72
                        base = int(fixture['pigments'][pigment], 16)
                        rgb = sum(max(0, min(255, int(((base>>shift)&255)*light+.5))) << shift
                                  for shift in (0, 8, 16))
                        # A power-of-two depth scale preserves computed ties;
                        # shading comes solely from the separately supplied RGB.
                        colored.extend([triangle[j], triangle[j+1], str(q*2), f'#{rgb:06x}'])
                    colored.append(triangle[9])
                commands += ['check zdraw raster triangles old '+shlex.join(original),
                             'check zdraw raster triangles-rgb new '+shlex.join(colored)]
            commands += ['check zdraw raster read old a', 'check zdraw raster read new b',
                         'print -r -- "$a"', 'print -r -- "$b"',
                         'check zdraw raster resolve new small 125 66',
                         'check zdraw raster info small info',
                         '(( info[width] == 125 && info[height] == 66 )) || fail resolve']
            # Use a script file: the captured batch exceeds exec's argument bound.
            with tempfile.NamedTemporaryFile(mode='w', dir=ROOT/'.build') as script:
                script.write('module_path=("$1"); zmodload zdraw || exit 1\ncheck() { "$@" || exit 1; }\n'+'\n'.join(commands))
                script.flush()
                output = subprocess.check_output([test_features.ZSH, '-df', script.name, str(ROOT/'.build/modules')], text=True)
            a, b = (line.split() for line in output.splitlines())
            self.assertEqual(a[::2], b[::2])
            self.assertEqual([float(v)*2 for v in a[1::2]], list(map(float, b[1::2])))

    def test_validation_palette_lifecycle_and_atomicity(self):
        self.assertIn('PASS', run(r'''
            check zdraw raster create-rgb s 4 4 '#123456' '.' '#AbCdEf' '#'
            check zdraw raster rectangles s 0 0 4 4 1 1 1
            check zdraw raster read-rgb s before
            for bad in '#12345' red 255 '#gg0000' '#1234567' ''; do
                reject zdraw raster triangles-rgb s 0 0 2 '#ffffff' 4 0 2 '#ffffff' 0 4 2 '#ffffff' 0 0 0 3 "$bad" 4 0 3 '#ffffff' 0 4 3 '#ffffff' 0
            done
            reject zdraw raster resolve s s 2 2
            reject zdraw raster resolve s out 5 4
            reject zdraw raster resolve s out 0 1
            check zdraw raster read-rgb s after
            [[ $before == $after ]] || fail atomicity
            typeset -a expected
            repeat 16; do expected+=('#abcdef' 1); done
            [[ $after == $expected ]] || fail material
            check zdraw raster create indexed 1 1 0 '.'
            reject zdraw raster triangles-rgb indexed
            reject zdraw raster read-rgb indexed after
            reject zdraw raster resolve indexed out 1 1
            check zdraw raster clear s 0
            check zdraw raster resolve s out 2 2
            check zdraw raster info out info
            (( info[rgb] && info[width] == 2 && info[height] == 2 )) || fail info
            check zdraw raster resize s 2 2
            check zdraw raster read-rgb s after
            [[ $after == '#123456 0 #123456 0 #123456 0 #123456 0' ]] || fail resize
            check zdraw raster free s
            check zdraw raster free out
            check zdraw raster free indexed
            check zdraw resourceinfo info
            (( !info[raster_bytes] )) || fail free
            print PASS
        '''))

    def test_terminal_contract_and_fallbacks(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '.build') as tmp:
            subprocess.run(['tic', '-x', '-o', tmp, str(ROOT/'tests/truecolor.terminfo')], check=True, capture_output=True)
            for mode, term in (('normal', 'zdraw-test-rgb'), ('mono', 'vt100')):
                drawing_session(self, mode, env={'TERM': term, 'TERMINFO': tmp}, fixture='raster-rgb.zsh', marker=b'RASTER RGB PASS')

    def test_budget_preflight_and_narrow_writer(self):
        source = (ROOT/'Src/Modules/zdraw.c').read_text()
        with tempfile.TemporaryDirectory(dir=ROOT / '.build') as terminfo:
            subprocess.run(['tic', '-x', '-o', terminfo, str(ROOT/'tests/truecolor.terminfo')], check=True, capture_output=True)
            for mode, definitions in (('budget', '#undef SHRT_MAX\n#define SHRT_MAX 1'), ('allocation_failure', '#define init_extended_pair(p, f, b) ((p) == 2 ? ERR : init_extended_pair(p, f, b))'), ('narrow', '#undef HAVE_WADD_WCHNSTR\n#undef HAVE_SETCCHAR\n#undef HAVE_GETCCHAR\n#undef HAVE_WIN_WCH')):
                with self.subTest(mode=mode), tempfile.TemporaryDirectory(dir=ROOT/'.build') as tmp:
                    # The limit helper uses SHRT_MAX after the system headers.
                    anchor = 'static int\nzdraw_pair_limit(void)' if mode == 'budget' else '#include <stdio.h>'
                    modules = test_features.FeatureTests().variant(tmp, source.replace(anchor, definitions+'\n'+anchor, 1))
                    drawing_session(self, mode, modules, {'TERM': 'zdraw-test-rgb', 'TERMINFO': terminfo}, 'raster-rgb.zsh', b'RASTER RGB PASS')


if __name__ == '__main__':
    unittest.main(verbosity=2)

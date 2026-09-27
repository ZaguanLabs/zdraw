"""Non-allocating pair plans compared with actual curses allocation/drawing."""
from pathlib import Path
import subprocess
import tempfile
import unittest

import test_features
from test_drawing import drawing_session

ROOT = test_features.ROOT


class ColorPlanTests(unittest.TestCase):
    def session(self, mode='normal', modules=None, env=None):
        return drawing_session(self, mode, modules, env,
                               fixture='colorplan.zsh', marker=b'COLORPLAN PASS')

    def test_headless_rejection(self):
        self.assertEqual(test_features.FeatureTests().run_shell('''
            zmodload zdraw || exit 1
            (( ${zdraw_features[(Ie)colorplan]} )) || exit 2
            typeset -A plan=(keep value)
            zdraw colorplan plan attr red/black 2>/dev/null && exit 3
            [[ $plan[keep] == value && ${#plan} == 1 ]] || exit 4
            (( ${#zdraw_windows} == 0 )) || exit 5
        '''), '')

    def test_prediction_assignment_and_lifecycle(self):
        self.session()

    def test_monochrome(self):
        self.session('monochrome', env={'TERM': 'vt100'})

    def test_no_terminal_output(self):
        self.assertEqual(self.session('silent'), self.session('silent_control'))

    def test_rgb(self):
        features = test_features.FeatureTests().run_shell(
            'zmodload zdraw || exit 1; print -rl -- "${zdraw_features[@]}"')
        if 'truecolor' not in features.splitlines():
            self.skipTest('RGB library interface unavailable')
        with tempfile.TemporaryDirectory(prefix='colorplan-rgb-', dir=ROOT / '.build') as tmp:
            terminfo = str(Path(tmp) / 'terminfo')
            subprocess.run(['tic', '-x', '-o', terminfo, str(ROOT / 'tests/truecolor.terminfo')],
                           capture_output=True, text=True, check=True)
            self.session('rgb', env={'TERM': 'zdraw-test-rgb', 'TERMINFO': terminfo})

    def test_limits_and_optional_paths(self):
        source = (ROOT / 'Src/Modules/zdraw.c').read_text()
        variants = {
            'narrow': '#undef HAVE_SETCCHAR\n#undef HAVE_GETCCHAR\n#undef HAVE_WIN_WCH',
            'no_spans': '#undef HAVE_SETCCHAR\n#undef HAVE_WADDCHNSTR',
            'failed_start': '#undef start_color\n#define start_color() ERR',
            'defaults_failed': '#undef use_default_colors\n#define use_default_colors() ERR',
            'no_defaults': '#undef HAVE_USE_DEFAULT_COLORS',
            'allocation_failure': '#undef init_pair\n#define init_pair(p, f, b) ERR',
            'small': '#undef COLOR_PAIRS\n#define COLOR_PAIRS 4',
        }
        for mode, definitions in variants.items():
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(
                    prefix='colorplan-', dir=ROOT / '.build') as tmp:
                modules = test_features.FeatureTests().variant(
                    tmp, source.replace('#include <stdio.h>', '#include <stdio.h>\n' + definitions, 1))
                self.session(mode, modules)


if __name__ == '__main__':
    unittest.main(verbosity=2)

"""Developer color resolution, real curses output, fallback and lifecycle."""
import subprocess
import tempfile
import unittest
import test_clipping
import test_features
from test_drawing import drawing_session


class ColorHelperTests(unittest.TestCase):
    def test_headless_resolution(self):
        test_clipping.ClippingTests().headless(fixture='ui-colors.zsh', marker='UI COLORS PASS')

    def test_runtime_profiles(self):
        with tempfile.TemporaryDirectory(dir=test_features.ROOT / '.build') as directory:
            subprocess.run(['tic', '-x', '-o', directory,
                            str(test_features.ROOT / 'tests/truecolor.terminfo')], check=True,
                           capture_output=True)
            features = test_features.FeatureTests().run_shell(
                'zmodload zdraw || exit 1; print -rl -- "${zdraw_features[@]}"')
            terminals = [('xterm-256color', '256'), ('xterm', '16'), ('vt100', 'mono')]
            if 'truecolor' in features.splitlines():
                terminals.append(('zdraw-test-rgb', 'rgb'))
                terminals.append(('zdraw-test-rgb-exact', 'rgb'))
            for terminal, profile in terminals:
                with self.subTest(terminal=terminal):
                    output = drawing_session(self, profile,
                        env={'TERM': terminal, 'TERMINFO': directory},
                        fixture='color-helpers.zsh', marker=b'COLOR HELPERS PASS')
                    if profile == 'rgb':
                        self.assertIn(b'38;2;94;218;200m', output)
                        self.assertIn(b'48;2;255;0;0m', output)
                    elif profile == '256':
                        self.assertIn(b'38;5;80m', output)
                        self.assertNotIn(b'38;2;', output)


if __name__ == '__main__':
    unittest.main(verbosity=2)

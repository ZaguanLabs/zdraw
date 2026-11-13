"""Safe serialization, conservative occupancy and restore failure cleanup."""
from pathlib import Path
import shutil
import tempfile
import unittest
import test_features
from test_drawing import drawing_session


class ScreenTests(unittest.TestCase):
    def test_roundtrip_and_invalid_data(self):
        drawing_session(self, 'normal', fixture='screen.zsh', marker=b'SCREEN PASS')

    def test_preserves_shell_terminfo(self):
        root = test_features.ROOT
        with tempfile.TemporaryDirectory(dir=root / '.build') as tmp:
            modules = Path(tmp) / 'modules'
            shutil.copytree(root / '.build/modules', modules)
            suffix = next(modules.glob('zdraw.*')).suffix
            shutil.copy2(root / f'.build/zsh/Src/Modules/terminfo{suffix}',
                         modules / f'zsh/terminfo{suffix}')
            drawing_session(self, 'terminfo', modules, fixture='screen.zsh', marker=b'SCREEN PASS')

    def test_preserves_inactive_stock_screen(self):
        root = test_features.ROOT
        with tempfile.TemporaryDirectory(dir=root / '.build') as tmp:
            modules = Path(tmp) / 'modules'
            shutil.copytree(root / '.build/modules', modules)
            suffix = next(modules.glob('zdraw.*')).suffix
            shutil.copy2(root / f'.build/zsh/Src/Modules/curses{suffix}',
                         modules / f'zsh/curses{suffix}')
            config = (test_features.BUILD / 'config.h').read_text().splitlines()
            safe = '#define HAVE_ZDRAW_SAFE_DELSCREEN 1' in config
            mode = 'stock' if safe else 'stock_guarded'
            with self.subTest(delscreen_preserves_other_screens=safe):
                drawing_session(self, mode, modules, fixture='screen.zsh', marker=b'SCREEN PASS')

    def test_rejects_unsafe_foreign_screen_teardown(self):
        root = test_features.ROOT
        source = (root / 'Src/Modules/zdraw.c').read_text()
        source = source.replace('#include <stdio.h>',
                                '#include <stdio.h>\n#undef HAVE_ZDRAW_SAFE_DELSCREEN', 1)
        with tempfile.TemporaryDirectory(dir=root / '.build') as tmp:
            modules = test_features.FeatureTests().variant(tmp, source)
            (modules / 'zsh').mkdir()
            suffix = next(modules.glob('zdraw.*')).suffix
            shutil.copy2(root / f'.build/zsh/Src/Modules/curses{suffix}',
                         modules / f'zsh/curses{suffix}')
            drawing_session(self, 'stock_guarded', modules, fixture='screen.zsh', marker=b'SCREEN PASS')

    def test_seeded_stock_screen_lifecycle(self):
        root = test_features.ROOT
        with tempfile.TemporaryDirectory(dir=root / '.build') as tmp:
            modules = Path(tmp) / 'modules'
            shutil.copytree(root / '.build/modules', modules)
            suffix = next(modules.glob('zdraw.*')).suffix
            shutil.copy2(root / f'.build/zsh/Src/Modules/curses{suffix}', modules / 'zsh')
            config = (test_features.BUILD / 'config.h').read_text().splitlines()
            drawing_session(self, 'stock', modules, fixture='lifecycle-stress.zsh',
                            env={'ZDRAW_STRESS_CYCLES': '40', 'ZDRAW_STRESS_SEED': '20261113',
                                 'ZDRAW_STRESS_GUARDED': str(int('#define HAVE_ZDRAW_SAFE_DELSCREEN 1' not in config))},
                            marker=b'LIFECYCLE STRESS PASS')

    def test_failed_screen_initialization(self):
        source = (test_features.ROOT / 'Src/Modules/zdraw.c').read_text()
        source = source.replace('#include <stdio.h>',
                                '#include <stdio.h>\n#define newterm(a,b,c) ((SCREEN *)0)\n#define initscr() ((WINDOW *)0)', 1)
        with tempfile.TemporaryDirectory(dir=test_features.ROOT / '.build') as tmp:
            modules = test_features.FeatureTests().variant(tmp, source)
            self.assertEqual(test_features.FeatureTests().run_shell('''
                zmodload zdraw || exit 1
                zdraw init && exit 2
                (( $? == 1 && ${#zdraw_windows} == 0 )) || exit 3
                typeset -A info
                zdraw colorinfo info || exit 4
                [[ $info[initialized] == 0 ]] || exit 5
                zmodload -u zdraw || exit 6
            ''', modules, terminal='xterm-256color'), '')

    def test_native_failures_release_prepared_rows(self):
        source = (test_features.ROOT / 'Src/Modules/zdraw.c').read_text()
        for mode, definition in (
            ('allocation_failure', '#define init_pair(a,b,c) ERR'),
            ('write_failure', '#define wadd_wchnstr(a,b,c) ERR'),
        ):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(dir=test_features.ROOT / '.build') as tmp:
                variant = source.replace('#include <stdio.h>', '#include <stdio.h>\n' + definition, 1)
                modules = test_features.FeatureTests().variant(tmp, variant)
                drawing_session(self, mode, modules, fixture='screen.zsh', marker=b'SCREEN PASS')

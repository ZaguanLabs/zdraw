"""Passive resource discovery, lifetime accounting and target validation."""
import unittest
from test_drawing import drawing_session
import test_features


class ResourceTests(unittest.TestCase):
    def test_headless_and_destinations(self):
        script = r'''
            zmodload zdraw || exit 1
            typeset -A info=(sentinel yes)
            zdraw resourceinfo info || exit 2
            [[ $info[format] == zdraw-resources-1 && $info[session] == inactive &&
               $info[windows] == 0 && $info[prepared_draws] == 0 && ! -v 'info[sentinel]' ]] || exit 3
            typeset scalar=sentinel
            typeset -a array=(sentinel)
            typeset -Ar immutable=(sentinel yes)
            for target in scalar array immutable 'info[x]' 'x;evil=1' path; do
                zdraw resourceinfo "$target" 2>/dev/null && exit 4
            done
            [[ $scalar == sentinel && $array == sentinel && $immutable[sentinel] == yes && ! -v evil ]] || exit 5
            zdraw resourceinfo created || exit 6
            [[ ${(t)created} == association && $created[windows] == 0 ]] || exit 7
            # Discovery cannot consume a waiting shell input stream.
            () {
                zdraw resourceinfo info || exit 8
                read -r line || exit 9
                [[ $line == pending ]] || exit 10
            } <<<pending
            print PASS
        '''
        for terminal in (None, 'zdraw-nonexistent-terminal'):
            self.assertEqual(test_features.FeatureTests().run_shell(script, terminal=terminal), 'PASS\n')

    def test_lifecycle(self):
        drawing_session(self, 'wide', fixture='resources.zsh', marker=b'RESOURCES PASS')

    def test_seeded_lifecycle_stress(self):
        drawing_session(self, 'standalone', fixture='lifecycle-stress.zsh',
                        env={'ZDRAW_STRESS_CYCLES': '40', 'ZDRAW_STRESS_SEED': '20261113'},
                        marker=b'LIFECYCLE STRESS PASS')

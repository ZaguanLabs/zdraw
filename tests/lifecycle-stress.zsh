#!/usr/bin/env zsh
# Seeded mixed lifetimes; the optional driver runs longer leak-checked cohorts.
emulate -R zsh
setopt nounset
module_path=("$1")
typeset mode=$2
typeset cycles=${ZDRAW_STRESS_CYCLES:-40} seed=${ZDRAW_STRESS_SEED:-20261113}
[[ $cycles == <-> && ${#cycles} -le 5 && $seed == <-> && ${#seed} -le 9 ]] || exit 2
(( cycles > 0 && cycles <= 10000 )) || exit 2
typeset -i rng=$((10#$seed)) cycle=0 h w choice init_status
typeset -A resources cell before after
typeset key
fail() { print -ru2 -- "FAIL seed=$seed cycle=$cycle: $*"; exit 1; }
check() { "$@" || fail "$*"; }
reject() { "$@" 2>/dev/null && fail "unexpected success: $*"; return 0; }
random() { (( rng = (1664525 * rng + 1013904223) & 0xffffffff )); }
empty_resources() {
  check zdraw resourceinfo resources
  [[ $resources[session] == inactive ]] || fail 'session remains active'
  for key in windows pads pad_cells private_input_pads retired_tree_windows cached_color_pairs prepared_rows prepared_bytes; do
    [[ $resources[$key] == 0 ]] || fail "unreleased $key=$resources[$key]"
  done
}
check zmodload zdraw
if [[ $mode == stock ]]; then
  check zmodload zsh/curses
elif [[ $mode != standalone ]]; then
  exit 2
fi
for (( cycle=1; cycle<=cycles; cycle++ )); do
  if [[ $mode == stock ]]; then
    check zcurses init
    check zcurses move stdscr 0 0
    check zcurses attr stdscr blue/black
    check zcurses string stdscr before
    check zcurses end
  fi
  zdraw init
  init_status=$?
  if (( init_status != 0 )); then
    [[ $init_status == 1 && $mode == stock && ${ZDRAW_STRESS_GUARDED:-0} == 1 ]] || fail init
    empty_resources
  else
    [[ ${ZDRAW_STRESS_GUARDED:-0} == 0 || $mode == standalone ]] || fail 'guard unexpectedly accepted foreign screen'
    {
      random; h=$((4 + rng % 5))
      random; w=$((12 + rng % 20))
      check zdraw addwin sample "$h" "$w" 1 1
      check zdraw addwin child 2 6 2 2 sample
      check zdraw addpad pad "$((h+3))" "$((w+5))"
      check zdraw prepare row bold,red/black $'e\u0301界 abc'
      check zdraw draw sample 0 0 row
      check zdraw spans child 0 0 green/black child
      check zdraw snapshot sample before
      reject zdraw treewin sample 1 1 1 1
      reject zdraw prepare row '' duplicate
      reject zdraw draw sample "$h" 0 row
      check zdraw snapshot sample after
      [[ ${(kv)before} == ${(kv)after} ]] || fail 'rejected operation changed cells'
      check zdraw treewin sample "$((h+1))" "$((w+1))" 2 3
      check zdraw move child 0 0
      check zdraw string child Q
      check zdraw move sample 1 1
      check zdraw cellinfo sample cell
      [[ $cell[text] == Q ]] || fail 'tree lost shared storage'
      check zdraw resizepad pad "$h" "$w"
      check zdraw copy sample 0 0 pad 0 0 "$h" "$w"
      check zdraw stage sample
      check zdraw present
      random; choice=$((rng % 3))
      if (( choice == 0 )); then
        check zdraw suspend
        check zdraw resume
      fi
      if (( choice != 1 )); then
        check zdraw unprepare row
        check zdraw delwin child
        check zdraw delwin sample
        check zdraw delwin pad
        # Reuse the cached name with unrelated geometry and color.
        check zdraw addwin sample 2 4 0 0
        check zdraw fill sample 0 0 2 4 blue/black Z
        check zdraw cellinfo sample cell
        [[ $cell[text] == Z && $cell[color] == blue/black ]] || fail 'stale lookup'
      fi
    } always {
      if (( choice == 2 )); then
        check zmodload -u zdraw
        check zmodload zdraw
      else
        check zdraw end
      fi
    }
    empty_resources
  fi
  check zmodload -u zdraw
  check zmodload zdraw
  empty_resources
  if [[ $mode == stock ]]; then
    check zcurses init
    check zcurses move stdscr 0 0
    check zcurses attr stdscr red/black
    check zcurses string stdscr preserved
    check zcurses end
  fi
done
check zmodload -u zdraw
[[ $mode != stock ]] || check zmodload -u zsh/curses
print -r -- "LIFECYCLE STRESS PASS cycles=$cycles seed=$seed mode=$mode"

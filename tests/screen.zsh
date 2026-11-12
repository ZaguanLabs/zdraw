#!/usr/bin/env zsh
emulate -R zsh
setopt nounset
module_path=("$1")
typeset mode=$2
zmodload zdraw || exit 1
source "${0:A:h:h}/lib/zdraw-screen.zsh" || exit 1
fail() { print -ru2 -- "FAIL: $*"; exit 1; }
check() { "$@" || fail "$*"; }
reject() { "$@" 2>/dev/null && fail "unexpected success: $*"; return 0; }
# Pin the stock module while it is inactive: this also exercises the host
# configuration where libncurses remains resident across zdraw unloads.
if [[ $mode == stock || $mode == stock_guarded ]]; then
  check zmodload zsh/curses
  repeat 3; do
    check zcurses init
    check zcurses attr stdscr blue/black
    check zcurses string stdscr stock
    check zcurses end
    if [[ $mode == stock_guarded ]]; then
      zdraw init && fail 'unsafe foreign-screen initialization succeeded'
      (( $? == 1 && ${#zdraw_windows} == 0 )) || fail 'guard changed zdraw state'
      # Rejection must preserve the actual foreign WINDOWs, not just avoid a
      # crash. Exercise them again before and after zdraw unload/reload.
      check zcurses init
      check zcurses attr stdscr red/black
      check zcurses string stdscr preserved
      check zcurses end
    else
      check zdraw init
      check zdraw spans stdscr 0 0 red/black zdraw
      check zdraw end
    fi
    check zmodload -u zdraw
    check zmodload zdraw
  done
  check zmodload -u zsh/curses
  print -r -- 'SCREEN PASS'
  exit 0
fi

typeset zdraw_screen_data saved REPLY
typeset -A pixels original after resources
if [[ $mode == terminfo ]]; then
  check zmodload zsh/terminfo
  typeset original_colors=$terminfo[colors]
fi
check zdraw init
{
  check zdraw addwin sample 2 8 1 1
  check zdraw addwin target 2 8 4 1
  if [[ $mode == write_failure ]]; then
    saved='zdraw-screen-1 2 8 0 0'
    repeat 16; do saved+=$'\n-:41'; done
    reject zdraw-screen-restore target "$saved"
  elif [[ $mode == allocation_failure ]]; then
    saved='zdraw-screen-1 2 8 0 0'
    repeat 8; do saved+=$'\n-:41'; done
    repeat 8; do saved+=$'\n7265642f626c61636b:42'; done
    reject zdraw-screen-restore target "$saved"
    check zdraw snapshot target pixels
    [[ $pixels[0,0,text] == ' ' ]] || fail 'prepare failure changed destination'
  else
    check zdraw spans sample 0 0 bold,red/black 'ab' '' $'e\u0301' underline 'q'
    check zdraw move sample 1 3
    check zdraw snapshot sample original
    check zdraw-screen-save sample
    saved=$zdraw_screen_data
    check zdraw prepare zdraw_screen_${$}_0 '' OWN
    check zdraw-screen-restore target "$saved"
    check zdraw rowinfo zdraw_screen_${$}_0 resources
    [[ $resources[width] == 3 && $resources[draws] == 0 ]] || fail 'borrowed row changed'
    check zdraw unprepare zdraw_screen_${$}_0
    check zdraw snapshot target after
    [[ "${(j:|:)${(@kv)original}}" == "${(j:|:)${(@kv)after}}" ]] || fail 'roundtrip differs'
    check zdraw-screen-save target
    [[ $zdraw_screen_data == "$saved" ]] || fail 'serialization differs'
    reject zdraw-screen-restore target "${saved/zdraw-screen-1/zdraw-screen-2}"
    reject zdraw-screen-restore target "$saved"$'\n-:41'
    reject zdraw-screen-restore target "${saved/:20/:00}"
    reject zdraw-screen-restore target "${saved/:20/:1b}"
    reject zdraw-screen-restore target "${saved/:20/:4142}"
    reject zdraw-screen-restore target "${saved/:20/:zz}"
    reject zdraw-screen-restore target "${saved/626f6c642c7265642f626c61636b/626f677573}"
    reject zdraw-screen-restore target "${saved/2 8/3 8}"
    reject zdraw-screen-restore target "${(pl:1048577::x:)saved}"
    check zdraw snapshot target pixels
    [[ "${(j:|:)${(@kv)pixels}}" == "${(j:|:)${(@kv)after}}" ]] || fail 'invalid data changed destination'
    check zdraw spans sample 0 0 '' ' 界界 '
    check zdraw snapshot sample pixels occupancy
    [[ $pixels[0,1,occupancy] == base && $pixels[0,2,occupancy] == continuation && $pixels[0,3,occupancy] == base && $pixels[0,4,base_column] == 3 && $pixels[0,1,occupancy_source] == inferred ]] || fail 'wide occupancy'
    reject zdraw-screen-save sample
    [[ $zdraw_screen_data == "$saved" ]] || fail 'failed export replaced data'
    check zdraw spans sample 0 0 '' '界'
    check zdraw snapshot sample pixels occupancy
    [[ $pixels[0,0,occupancy] == unknown && $pixels[0,1,base_column] == unknown ]] || fail 'edge must be unknown'
    check zdraw addwin child 1 3 1 2 sample
    check zdraw snapshot child pixels occupancy
    [[ $pixels[0,0,occupancy] == unknown ]] || fail 'shared edge must be unknown'
    check zdraw spans target 0 0 '' 'fresh'
    check zdraw-screen-restore target "$saved"
    # Shift pair allocation in a fresh session; serialized styles remain usable.
    check zdraw end
    check zmodload -u zdraw
    check zmodload zdraw
    check zdraw init
    check zdraw addwin target 2 8 1 1
    check zdraw spans target 0 0 blue/black X
    check zdraw-screen-restore target "$saved"
    check zdraw-screen-save target
    [[ $zdraw_screen_data == "$saved" ]] || fail 'cross-session styles changed'
  fi
  check zdraw resourceinfo resources
  [[ $resources[prepared_rows] == 0 ]] || fail 'temporary rows leaked'
} always {
  zdraw end
}
if [[ $mode == terminfo ]]; then
  [[ $terminfo[colors] == $original_colors ]] || fail 'shell terminfo changed'
  check zmodload -u zsh/terminfo
fi
print -r -- 'SCREEN PASS'

#!/usr/bin/env zsh
emulate -R zsh
setopt nounset
module_path=("$1")
typeset mode=$2
zmodload zdraw || exit 1
fail() { print -ru2 -- "FAIL: $*"; exit 1; }
check() { "$@" || fail "$*"; }
reject() { "$@" 2>/dev/null && fail "unexpected success: $*"; return 0; }
typeset -a cell before after
typeset -A info saved
check zdraw raster create s 4 3 0 ' ' 1 '#'
check zdraw raster clear s 1 0
reject zdraw raster blit s stdscr 0 0 mono
check zdraw init
{
  check zdraw addwin sample 3 6 1 1
  check zdraw move sample 2 5
  check zdraw position sample before
  if [[ $mode == unavailable ]]; then
    zdraw raster blit s sample 0 0 mono
    (( $? == 2 )) || fail unsupported
  else
    check zdraw raster blit s sample 0 0 mono
    check zdraw position sample after
    [[ $before == $after ]] || fail cursor
    check zdraw move sample 0 0
    check zdraw querychar sample cell
    [[ $cell[1] == '#' ]] || fail mono
    check zdraw move sample 1 0
    check zdraw querychar sample cell
    [[ $cell[1] == ' ' ]] || fail odd_height
    check zdraw snapshot sample saved
    reject zdraw raster blit s sample 0 3 mono
    reject zdraw raster blit s sample 2 0 mono
    reject zdraw raster blit s sample -1 0 mono
    reject zdraw raster blit s sample 0 0 bogus
    check zdraw snapshot sample info
    [[ ${(kv)info} == ${(kv)saved} ]] || fail invalid_blit_changed_cells
    if [[ $mode == mono || $mode == pair_failure ]]; then
      reject zdraw raster blit s sample 0 0 half
      check zdraw raster blit s sample 0 0 mono
    else
      check zdraw raster blit s sample 0 0 ascii
      check zdraw move sample 0 0
      check zdraw querychar sample cell
      [[ $cell[1] == '#' && $cell[2] == 1/0 ]] || fail ascii
      if [[ $mode == narrow ]]; then
        zdraw raster blit s sample 0 0 half
        (( $? == 2 )) || fail half_unsupported
      else
        check zdraw raster blit s sample 0 0 half
        check zdraw move sample 0 0
        check zdraw querychar sample cell
        [[ $cell[1] == '▀' && $cell[2] == 1/0 ]] || fail half
        check zdraw raster free s
        check zdraw querychar sample cell
        [[ $cell[1] == '▀' ]] || fail retained_cells
        check zdraw raster create s 4 3 0 ' ' 1 '#'
        () {
          local LC_ALL=C
          zdraw raster blit s sample 0 0 half
          (( $? == 2 )) || fail locale
          check zdraw raster blit s sample 0 0 ascii
        }
      fi
    fi
    check zdraw stage sample
    check zdraw present
    check zdraw raster resize s 2 2
    check zdraw resizewin sample 2 3
    check zdraw raster blit s sample 0 0 mono
  fi
} always {
  zdraw end
}
check zdraw resourceinfo info
(( info[raster_bytes] == 0 )) || fail end
check zdraw init
reject zdraw raster read s after
check zdraw end
print -r -- 'RASTER PASS'

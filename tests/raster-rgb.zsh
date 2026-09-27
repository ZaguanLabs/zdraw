#!/usr/bin/env zsh
emulate -R zsh
setopt nounset
module_path=("$1")
typeset mode=$2
zmodload zdraw || exit 1
fail() { print -ru2 -- "FAIL: $*"; exit 1; }
check() { "$@" || fail "$*"; }
reject() { "$@" 2>/dev/null && fail "unexpected success: $*"; return 0; }
typeset -A info saved plan
typeset -a cell before after
check zdraw raster create-rgb s 2 3 '#112233' '.' '#445566' '#'
check zdraw raster rectangles s 0 0 2 1 2 2 1
check zdraw raster create-rgb t 1 2 '#778899' '*'
check zdraw init
{
  check zdraw addwin sample 3 6 1 1
  reject zdraw raster blit s sample 0 0 half
  reject zdraw raster plan s plan
  check zdraw colorinfo info
  (( info[pairs_used] == 0 )) || fail opt_in
  check zdraw raster blit s sample 0 0 mono
  if [[ $mode == mono ]]; then
    reject zdraw truecolor on
  else
    check zdraw truecolor on
    check zdraw raster plan s plan t
    (( plan[requested] == 5 && plan[unique] == 3 && plan[pairs_needed] == 3 && plan[pairs_used] == 0 )) || fail plan
    if [[ $mode == budget ]]; then
      (( !plan[fits] )) || fail fits
      check zdraw snapshot sample saved
      reject zdraw raster blit s sample 0 0 half
      check zdraw colorinfo info
      (( info[pairs_used] == 0 )) || fail allocated_on_budget_failure
      check zdraw snapshot sample info
      [[ ${(kv)info} == ${(kv)saved} ]] || fail changed_on_budget_failure
    elif [[ $mode == allocation_failure ]]; then
      (( plan[fits] )) || fail fits
      check zdraw snapshot sample saved
      reject zdraw raster blit s sample 0 0 half
      check zdraw colorinfo info
      (( info[pairs_used] == 1 )) || fail allocation_error_accounting
      check zdraw snapshot sample info
      [[ ${(kv)info} == ${(kv)saved} ]] || fail allocation_error_changed_cells
    else
      (( plan[fits] )) || fail fits
      check zdraw move sample 2 5
      check zdraw position sample before
      if [[ $mode == narrow ]]; then
        zdraw raster blit s sample 0 0 half
        (( $? == 2 )) || fail narrow
        check zdraw raster blit s sample 0 0 ascii
      else
        check zdraw raster blit s sample 0 0 half
      fi
      check zdraw position sample after
      [[ $before == $after ]] || fail cursor
      check zdraw move sample 0 0
      check zdraw querychar sample cell
      [[ $cell[2] == '#445566/#112233' ]] || fail "RGB pair $cell"
      [[ $cell[1] == '▀' || $mode == narrow ]] || fail glyph
      check zdraw move sample 1 0
      check zdraw querychar sample cell
      [[ $cell[2] == '#112233/#112233' ]] || fail odd_row
      check zdraw raster plan s plan t
      (( plan[pairs_reused] == 2 && plan[pairs_needed] == 1 && plan[pairs_used] == 2 )) || fail reuse
      check zdraw raster create-rgb black 1 1 '#000000' ' '
      check zdraw snapshot sample saved
      reject zdraw raster blit black sample 0 0 half
      check zdraw snapshot sample info
      [[ ${(kv)info} == ${(kv)saved} ]] || fail reserved_RGB
      check zdraw raster create-rgb large 512 128 '#112233' '.'
      typeset plan_before="${(kv)plan}"
      # Nine repeated surfaces exceed the aggregate cell bound; pair reuse
      # cannot bypass the work bound, and failure must preserve the target.
      reject zdraw raster plan large plan large large large large large large large large
      [[ "${(kv)plan}" == "$plan_before" ]] || fail plan_target
      check zdraw raster free large
      check zdraw suspend
      reject zdraw raster resolve s out 1 1
      check zdraw resume
      check zdraw resizewin sample 2 2
      reject zdraw raster blit s sample 0 1 mono
      check zdraw raster blit s sample 0 0 mono
      check zdraw raster free s
      check zdraw stage sample
      check zdraw present
    fi
  fi
} always {
  zdraw end
}
check zdraw resourceinfo info
(( info[raster_bytes] == 0 )) || fail cleanup
print -r -- 'RASTER RGB PASS'

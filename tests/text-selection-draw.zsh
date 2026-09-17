#!/usr/bin/env zsh
emulate -R zsh
setopt nounset
module_path=("$1")
zmodload zdraw || exit 1
source "${0:A:h:h}/lib/zdraw-text-selection.zsh" || exit 1
fail() { print -ru2 -- "FAIL: $*"; exit 1; }
check() { "$@" || fail "$*"; }
reject() { "$@" 2>/dev/null && fail "unexpected success: $*"; return 0; }
typeset -A zdraw_text_selection zdraw_selection_event original selected restored
typeset -a reply
typeset field profile
check zdraw init
{
  check zdraw mouse delay 0 motion
  reject zdraw mouse delay garbage
  reject zdraw mouse delay ''
  reject zdraw mouse delay 2147483648
  reject zdraw mouse delay -1
  for profile in color mono; do
    zdraw clear stdscr
    zdraw spans stdscr 2 2 underline '|'
    zdraw spans stdscr 2 9 bold 'SIDEBAR'
    typeset base=bold,green/black
    [[ $profile == mono ]] && base=bold
    check zdraw-text-selection-init 'a界éz' r 2 3 1 6 unicode-17.0.0-egc-wcwidth-sum-attach-zero \
      0 0 0 "$base" 'a界' 0 0 4 underline 'éz'
    check zdraw-text-selection-spans 0
    check zdraw spans stdscr 2 3 policy=unicode-17.0.0-egc-wcwidth-sum-attach-zero "${reply[@]}"
    check zdraw snapshot stdscr original
    zdraw_selection_event=(type mouse x 4 y 2 buttons PRESSED1 modifiers '')
    check zdraw-text-selection-event r
    zdraw_selection_event=(type mouse x 79 y 23 buttons RELEASED1 modifiers '')
    check zdraw-text-selection-event r
    check zdraw-text-selection-spans 0
    check zdraw spans stdscr 2 3 policy=unicode-17.0.0-egc-wcwidth-sum-attach-zero "${reply[@]}"
    check zdraw snapshot stdscr selected
    [[ $selected[2,4,attributes] != "$original[2,4,attributes]" ]] || fail highlight
    # Every cell outside the content rectangle retains every snapshot property.
    for field in "${(@k)original}"; do
      [[ $field == 2,[3-8],* ]] && continue
      [[ ${selected[$field]-} == "$original[$field]" ]] || fail "leak $field"
    done
    check zdraw-text-selection-clear
    check zdraw-text-selection-spans 0
    check zdraw spans stdscr 2 3 policy=unicode-17.0.0-egc-wcwidth-sum-attach-zero "${reply[@]}"
    check zdraw snapshot stdscr restored
    for field in "${(@k)original}"; do
      [[ ${restored[$field]-} == "$original[$field]" ]] || fail "restore $field"
    done
  done
  check zdraw mouse -motion
  check zdraw refresh
} always {
  zdraw end
}
print -r -- 'TEXT SELECTION DRAW PASS'

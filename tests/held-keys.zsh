#!/usr/bin/env zsh
emulate -R zsh
setopt nounset
typeset root=${0:A:h:h}
source "$root/examples/held-keys.zsh"
module_path=("$1")
typeset report_fd=$2 control_fd=$3 scenario=$4
typeset -A held event cap
typeset held_mode held_note
typeset -i held_focused i
fail() { print -ru2 -- "FAIL: $*"; exit 1; }
check() { "$@" || fail "$*"; }
step() { print -ru "$report_fd" -- "$1"; read -ru "$control_fd" || fail EOF; }
next-event() {
  local -i attempt
  for ((attempt=0; attempt<30; attempt++)); do
    zdraw event stdscr event norefresh && { held-demo-event; return 0; }
  done
  fail 'missing event'
}
check zmodload zdraw
step baseline
check zdraw init
{
  check zdraw timeout stdscr 50
  check zdraw inputdelay 25
  check zdraw spans stdscr 0 0 '' HELD_UNPRESENTED
  check zdraw stage stdscr
  check held-demo-start enhanced
  step focus-query
  check next-event
  if [[ $scenario == focus-timeout ]]; then
    [[ $held_mode == legacy && $event[phase] == timeout ]] || fail focus_timeout
    step late-focus
    check next-event
    [[ $held_mode == legacy ]] || fail late_focus_activated
  else
    [[ $held_mode == query-keyboard ]] || fail focus_activation
    step keyboard-query
    check next-event
    if [[ $scenario == keyboard-timeout ]]; then
      [[ $held_mode == legacy && $event[phase] == timeout ]] || fail keyboard_timeout
      step late-keyboard
      check next-event
      [[ $held_mode == legacy ]] || fail late_keyboard_activated
    else
      [[ $held_mode == enhanced ]] || fail keyboard_activation
      step multi
      check next-event
      check next-event
      (( ${#held} == 2 && ${+held[U+0077]} && ${+held[U+0064]} )) || fail multi
      check next-event
      (( ${#held} == 2 )) || fail repeat
      check next-event
      (( ${#held} == 1 && ${+held[U+0064]} )) || fail release_with_different_modifiers
      check next-event
      (( ${#held} == 0 )) || fail releases
      step focus-loss
      check next-event
      (( ${#held} == 1 )) || fail pressed
      check next-event
      (( !held_focused && !${#held} )) || fail lost_focus
      check next-event
      (( !${#held} )) || fail unfocused_press
      check next-event
      (( held_focused )) || fail focus_in
      check next-event
      (( !${#held} )) || fail repeat_reactivated
      check next-event
      (( ${#held} == 1 )) || fail fresh_press
      step legacy-arrow
      check next-event
      [[ $event[source] == curses && $event[key] == UP && ${#held} == 1 ]] || fail legacy_arrow
      step resize
      check next-event
      [[ $event[type] == resize && ${#held} == 0 ]] || fail resize
      check zdraw resize "$event[rows]" "$event[columns]" nosave
      step after-resize
      # Curses can also enqueue its own resize notice for the same change.
      for ((i=0;i<5;i++)); do
        check next-event
        [[ $event[type] != resize ]] && break
      done
      [[ $event[action] == repeat && ${#held} == 0 ]] || fail resize_repeat
      check next-event
      (( ${#held} == 1 )) || fail resize_press
      check held-demo-clear Suspended
      check zdraw suspend
      step suspended
      check zdraw resume
      step resumed
      check next-event
      [[ $event[type] == focus ]] || fail resumed_focus
      check next-event
      (( !${#held} )) || fail resume_repeat
      check next-event
      (( ${#held} == 1 )) || fail resume_press
      step malformed
      check next-event
      [[ $event[type] == unknown && ${#held} == 0 ]] || fail malformed
      check next-event
      (( !${#held} )) || fail malformed_repeat
      check next-event
      (( ${#held} == 1 )) || fail malformed_press
      step partial
      zdraw event stdscr event poll norefresh && fail 'partial delivered early'
      # End must clean up even with an incomplete packet and a live held key.
    fi
  fi
} always {
  held=()
  zdraw end
}
check zdraw resourceinfo cap
(( cap[private_input_pads] == 0 && cap[cached_color_pairs] == 0 )) || fail resources
step cleaned
check zmodload -u zdraw
check zmodload zdraw
check zdraw init
check zdraw capabilities cap
[[ $cap[keyboard_events,enabled] == no && $cap[focus_events,enabled] == no ]] || fail reload
check zdraw end
print -ru "$report_fd" -- done

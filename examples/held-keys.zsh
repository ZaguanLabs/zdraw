#!/usr/bin/env zsh
# Standalone input-contract inspector. These example functions can be sourced
# by tests without loading a module, reading input or changing terminal modes.
# State is local to main (or a test caller), using Zsh's dynamic function scope.
held-demo-clear() {
  emulate -L zsh
  held=()
  held_note=$1
}

held-demo-start() {
  emulate -L zsh
  held=() held_focused=1 held_mode=legacy
  held_note='Legacy input: release state is unavailable.'
  [[ $1 == legacy ]] && return 0
  if (( ${zdraw_features[(Ie)focus_events]} && ${zdraw_features[(Ie)keyboard_events]} )) &&
      zdraw query on && zdraw query request focus_events 500; then
    held_mode=query-focus
    held_note='Querying focus support; legacy input remains available.'
  fi
  return 0
}

held-demo-event() {
  emulate -L zsh
  local identity=${event[key]-} action=${event[action]-}
  case ${event[type]-} in
    capability)
      [[ ${event[phase]-} == (reply|timeout) ]] || return 0
      if [[ $held_mode == query-focus && ${event[name]-} == focus_events ]]; then
        held_mode=legacy
        held_note='Focus ownership unavailable; using legacy input.'
        if [[ $event[phase] == reply ]] && zdraw focus on 2>/dev/null &&
            zdraw query request keyboard_events 500; then
          held_mode=query-keyboard
          held_note='Querying keyboard support; legacy input remains available.'
        fi
      elif [[ $held_mode == query-keyboard && ${event[name]-} == keyboard_events ]]; then
        held_mode=legacy
        held_note='Keyboard negotiation unavailable; using legacy input.'
        if [[ $event[phase] == reply ]] && zdraw keyboard on 2>/dev/null; then
          held_mode=enhanced
          held_note='Keyboard and focus reporting enabled.'
        fi
      fi
      ;;
    focus)
      if [[ ${event[focused]-} == 1 ]]; then
        held_focused=1
        held_note='Focus returned; cleared keys require a fresh press.'
      else
        held_focused=0
        held-demo-clear 'Focus lost; held state cleared.'
      fi
      ;;
    resize) held-demo-clear 'Resized; held state cleared.' ;;
    unknown)
      [[ ${event[source]-} != kitty ]] || held-demo-clear 'Unrecognized keyboard packet; held state cleared.'
      ;;
    key|character)
      if [[ ${event[source]-} != kitty ]]; then
        held_note='Legacy event: no press/repeat/release state inferred.'
        return 0
      fi
      if [[ ${event[supported]-} != yes ]]; then
        held-demo-clear 'Unsupported identity; held state cleared.'
        return 0
      fi
      [[ $held_mode == enhanced && -n $identity ]] || return 0
      # Identity, not text or modifiers, pairs a release with its press.
      case $action in
        release) unset "held[$identity]" ;;
        press)
          if ((held_focused)); then
            if (( ${#held} >= 64 && ! ${+held[$identity]} )); then
              held-demo-clear 'Held-key limit reached; state cleared.'
              return 0
            fi
            held[$identity]=1
          fi ;;
        repeat) ;; # Never recreate a hold cleared by focus/resize/suspend.
        *) held-demo-clear 'Unknown key action; held state cleared.'; return 0 ;;
      esac
      held_note="$identity $action"
      ;;
  esac
  return 0
}

held-demo-draw() {
  emulate -L zsh
  local -a dimensions lines
  local -i row
  zdraw position stdscr dimensions || return
  lines=('zdraw held-key inspector - Q/Escape quits; Ctrl-Z suspends'
         "Mode: $held_mode | Focus: $held_focused | Held: ${#held}"
         "Keys: ${(j: :)${(ok)held}}"
         "$held_note"
         'Only negotiated key identities enter the held set.'
         'Repeat never adds a key. Release ignores modifier changes.'
         'Legacy arrows/characters remain legacy events.')
  zdraw clear stdscr || return
  for ((row=0; row<${#lines} && row<dimensions[5]; row++)); do
    zdraw spansclip stdscr $row 0 $dimensions[6] '' "$lines[row+1]" || return
  done
  zdraw stage stdscr && zdraw present
}

held-demo-main() {
  emulate -L zsh
  setopt localtraps nounset
  local root=$1 requested=${2:-enhanced}
  [[ $# -le 2 && $requested == (enhanced|--legacy) ]] || {
    print -ru2 -- 'Usage: held-keys.zsh [--legacy]'; return 2
  }
  [[ $requested != --legacy ]] || requested=legacy
  local -a module_path=("$root/.build/modules") dimensions
  local -A held event
  local held_mode held_note
  local -i held_focused running=1 result=0 dirty=1 drained suspend_requested=0
  local -F 6 SECONDS suspend_deadline=0
  zmodload zdraw || return
  (( ${zdraw_features[(Ie)structured_events]} && ${zdraw_features[(Ie)norefresh_events]} &&
     ${zdraw_features[(Ie)staged_refresh]} && ${zdraw_features[(Ie)clipped_spans]} )) || {
    print -ru2 -- 'This inspector requires structured events, norefresh, stage and spansclip.'
    return 2
  }
  zdraw init || return
  {
    trap 'running=0; result=130; held-demo-clear Interrupted' INT
    trap 'running=0; result=143; held-demo-clear Terminated' TERM
    trap 'running=0; result=129; held-demo-clear Disconnected' HUP
    trap 'suspend_requested=1; held-demo-clear Suspended' TSTP
    zdraw timeout stdscr 100 || return
    if (( ${zdraw_features[(Ie)input_delay]} )); then zdraw inputdelay 25 || return; fi
    held-demo-start "$requested"
    while ((running)); do
      if ((suspend_requested)); then
        held-demo-clear Suspended
        ((suspend_deadline)) || suspend_deadline=$((SECONDS+0.75))
        if zdraw suspend; then
          [[ $held_mode != query-* ]] || held_mode=legacy
          # A resumed background job stops again without reading the terminal.
          while ((running)); do
            kill -STOP $$
            ((running)) || break
            zdraw resume 2>/dev/null && break
          done
          ((running)) || break
          suspend_requested=0 suspend_deadline=0 held_focused=1 dirty=1
          held-demo-clear 'Resumed; held state cleared.'
          zdraw geometry dimensions || return
          zdraw resize "$dimensions[1]" "$dimensions[2]" nosave || return
        elif ((SECONDS>=suspend_deadline)); then
          # A partial keyboard packet can temporarily prevent suspension.
          result=1
          break
        fi
      fi
      if ((dirty && !suspend_requested)); then
        held-demo-draw || return
        dirty=0
      fi
      # One bounded wait, then at most 31 immediately available events.
      for ((drained=0; drained<32 && running; drained++)); do
        if ((drained)); then zdraw event stdscr event poll norefresh || break
        else zdraw event stdscr event norefresh || break; fi
        if [[ ${event[source]-} == kitty ]]; then
          if [[ ${event[action]-} == press ]]; then
            if [[ ${event[key]-} == (ESC|U+0071|U+0051) ]]; then running=0
            elif [[ " ${event[modifiers]-} " == *' CTRL '* ]]; then
              case ${event[key]-} in
                U+0063) running=0 result=130 ;;
                U+007A) suspend_requested=1 ;;
              esac
            fi
          fi
        elif [[ ${event[type]-} == character ]]; then
          case ${event[text]-} in
            q|Q|$'\e') running=0 ;;
            $'\C-c') running=0 result=130 ;;
            $'\C-z') suspend_requested=1 ;;
          esac
        fi
        if [[ $event[type] == resize ]]; then
          zdraw resize "$event[rows]" "$event[columns]" nosave || return
        fi
        held-demo-event
        (( !suspend_requested )) || held-demo-clear Suspended
        dirty=1
      done
    done
  } always {
    held=()
    zdraw end
  }
  return $result
}

if [[ $ZSH_EVAL_CONTEXT != *:file ]]; then
  held-demo-main "${0:A:h:h}" "$@"
fi

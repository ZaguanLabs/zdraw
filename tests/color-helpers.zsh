#!/usr/bin/env zsh
emulate -R zsh
setopt nounset
module_path=("$1")
typeset mode=$2 root=${0:A:h:h}
zmodload zdraw || exit 1
source "$root/lib/zdraw-ui.zsh" || exit 1
fail() { print -ru2 -- "FAIL: $*"; exit 1; }
check() { "$@" || fail "$*"; }
reject() { "$@" 2>/dev/null && fail "unexpected success: $*"; return 0; }
typeset -A zdraw_color=(sentinel unchanged) zdraw_ui_theme zdraw_ui_style info before after cell
typeset REPLY
typeset -a reply
reject zdraw-color-setup
[[ $zdraw_color[sentinel] == unchanged ]] || fail 'setup before init changed output'
unset NO_COLOR
check zdraw init
{
  check zdraw colorinfo before
  # Invalid theme overrides must not enable RGB or change the theme.
  zdraw_ui_theme=(sentinel unchanged)
  reject zdraw-ui-theme dark auto accent=invalid
  [[ $zdraw_ui_theme[sentinel] == unchanged ]] || fail 'invalid theme changed output'
  check zdraw colorinfo after
  [[ $before[truecolor_enabled] == $after[truecolor_enabled] ]] || fail 'invalid theme enabled RGB'
  check zdraw-color-setup
  [[ $zdraw_color[profile] == $mode ]] || fail "profile $zdraw_color[profile] != $mode"
  check zdraw colorinfo after
  [[ $before[pairs_used] == $after[pairs_used] ]] || fail 'setup allocated pairs'
  check zdraw-color '#5edac8'
  check zdraw-ui-theme dark auto 'accent=#5edac8'
  [[ $zdraw_ui_theme[accent] == $REPLY && $zdraw_ui_theme[profile] == $mode ]] || fail 'theme color'
  check zdraw-ui-style normal 'fg=#5edac8' bg=canvas
  [[ $zdraw_ui_style[fg] == $REPLY ]] || fail 'literal fallback'
  check zdraw-label stdscr 1 1 20 'Color helper' normal 'fg=#5edac8'
  check zdraw move stdscr 1 1
  check zdraw cellinfo stdscr cell
  if [[ $mode == mono ]]; then
    [[ $cell[pair] == 0 ]] || fail 'mono allocated pair'
  else
    [[ $cell[color] == "$REPLY/$zdraw_ui_theme[surface]" ]] || fail 'retained helper color'
  fi
  check zdraw-color-gradient '#ff0000' '#0000ff' 12
  typeset -a spans=()
  typeset color
  for color in "${reply[@]}"; do
    if [[ $color == default ]]; then spans+=('' '='); else spans+=("$color/$color" ' '); fi
  done
  check zdraw spans stdscr 3 1 "${spans[@]}"
  if [[ $mode == rgb ]]; then
    check zdraw-color-setup 256
    check zdraw-color 196
    [[ $REPLY == '#ff0000' ]] || fail 'forced indexed encoding'
    check zdraw spans stdscr 4 1 "$REPLY/$REPLY" 'red'
    check zdraw-color-setup 16
    check zdraw-color red
    check zdraw colorinfo info
    if [[ $info[rgb_min] == 0 ]]; then
      [[ $REPLY == '#800000' ]] || fail 'basic profile on an exact RGB entry'
    else
      [[ $REPLY == 1 ]] || fail 'basic profile with reserved ANSI colors'
    fi
  else
    reject zdraw-color-setup rgb
  fi
  NO_COLOR=1
  check zdraw-ui-theme dark auto
  [[ $zdraw_ui_theme[profile] == mono ]] || fail 'NO_COLOR selection'
  check zdraw-ui-style selected reverse fg=text bg=selection
  [[ $zdraw_ui_style[style] == reverse ]] || fail 'mono state style'
  reject zdraw-color-setup ''
  if [[ $mode != mono ]]; then
    check zdraw-color-setup 16
    [[ $zdraw_color[profile] == 16 ]] || fail 'explicit profile did not override NO_COLOR'
  fi
  check zdraw refresh stdscr
} always {
  zdraw end
}
reject zdraw-color-setup
check zdraw init
{
  unset NO_COLOR
  check zdraw-color-setup
  [[ $zdraw_color[profile] == $mode ]] || fail 'reinitialize capability snapshot'
} always {
  zdraw end
}
print -r -- 'COLOR HELPERS PASS'

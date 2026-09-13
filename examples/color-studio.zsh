#!/usr/bin/env zsh
# A palette authored once in RGB, adapted to the initialized curses session.
emulate -R zsh
setopt nounset
typeset studio_root=${0:A:h:h} theme_name=dark profile key
typeset -i mono=0 rows columns exit_code=0
while (( $# )); do
  case $1 in
    --theme) (( $# >= 2 )) && [[ $2 == (dark|light) ]] || exit 1; theme_name=$2; shift 2 ;;
    --profile) (( $# >= 2 )) && [[ $2 == (auto|mono) ]] || exit 1; [[ $2 == mono ]] && mono=1; shift 2 ;;
    *) print -ru2 -- 'Usage: color-studio.zsh [--theme dark|light] [--profile auto|mono]'; exit 1 ;;
  esac
done
module_path=("$studio_root/.build/modules")
zmodload zdraw || exit 1
source "$studio_root/lib/zdraw-ui.zsh" || exit 1
typeset -A zdraw_ui_theme zdraw_ui_style zdraw_color event
typeset -a dimensions reply cool warm gray input_options
(( ${zdraw_features[(Ie)norefresh_events]} )) && input_options+=(norefresh)

function studio-theme {
  emulate -L zsh
  local NO_COLOR=${NO_COLOR-}
  (( mono )) && NO_COLOR=1
  zdraw-ui-theme "$theme_name" auto || return
  zdraw-color-setup || return
  profile=$zdraw_color[profile]
  zdraw-color-gradient '#3265d8' '#64f0c2' 48 || return
  cool=("${reply[@]}")
  zdraw-color-gradient '#ffc66d' '#ef527d' 48 || return
  warm=("${reply[@]}")
  zdraw-color-gradient '#202020' '#e0e0e0' 48 || return
  gray=("${reply[@]}")
}

function studio-ramp {
  emulate -L zsh
  local -i ramp_row=$1 ramp_width=$2 ramp_column ramp_index
  shift 2
  local -a ramp=("$@") spans=()
  local color
  for (( ramp_column=0; ramp_column<ramp_width; ++ramp_column )); do
    ramp_index=$((ramp_column*(${#ramp}-1)/(ramp_width>1?ramp_width-1:1)+1))
    color=$ramp[$ramp_index]
    if [[ $color == default ]]; then
      spans+=('reverse' ' ')
    else
      spans+=("$color/$color" ' ')
    fi
  done
  zdraw spans stdscr "$ramp_row" 2 "${spans[@]}"
}

function studio-render {
  emulate -L zsh
  zdraw position stdscr dimensions || return
  rows=$dimensions[5] columns=$dimensions[6]
  zdraw-ui-style normal fg=text bg=canvas || return
  zdraw fill stdscr 0 0 "$rows" "$columns" "$zdraw_ui_style[style]" ' ' || return
  if (( rows < 10 || columns < 24 )); then
    zdraw-label stdscr 0 0 "$columns" 'Color studio / q quit' normal bg=canvas || return
    zdraw refresh stdscr
    return
  fi
  local -i width=$((columns-4))
  (( width > 96 )) && width=96
  zdraw-label stdscr 1 2 "$width" 'COLOR STUDIO' normal bold fg=accent bg=canvas || return
  zdraw-label stdscr 2 2 "$width" "One palette, adapted to $profile colors" normal fg=muted bg=canvas || return
  zdraw-label stdscr 4 2 "$width" '01  Ocean / blue to mint' normal bg=canvas || return
  studio-ramp 5 "$width" "${cool[@]}" || return
  zdraw-label stdscr 7 2 "$width" '02  Ember / amber to rose' normal bg=canvas || return
  studio-ramp 8 "$width" "${warm[@]}" || return
  if (( rows >= 15 )); then
    zdraw-label stdscr 10 2 "$width" '03  Slate / depth and contrast' normal bg=canvas || return
    studio-ramp 11 "$width" "${gray[@]}" || return
  fi
  if (( rows >= 19 )); then
    local lavender='#bc9cff'
    [[ $theme_name == light ]] && lavender='#6d3eb8'
    zdraw-label stdscr 14 2 "$width" 'READY   Shared theme accent' normal fg=accent || return
    zdraw-label stdscr 15 2 "$width" 'LOCAL   A lavender instance override' normal "fg=$lavender" || return
    zdraw-label stdscr 16 2 "$width" 'ERROR   Meaning stays in the text' normal fg=error || return
  fi
  if (( rows >= 11 )); then
    zdraw-label stdscr "$((rows-1))" 2 "$width" 't theme   m monochrome   q quit' normal fg=muted bg=canvas || return
  fi
  zdraw refresh stdscr
}

zdraw init || exit 1
{
  studio-theme || exit 1
  while true; do
    studio-render || { exit_code=1; break; }
    zdraw event stdscr event "${input_options[@]}" || { exit_code=1; break; }
    case $event[type] in
      character)
        case $event[text] in
          q) break ;;
          t) if [[ $theme_name == dark ]]; then theme_name=light; else theme_name=dark; fi
             studio-theme || { exit_code=1; break; } ;;
          m) (( mono = !mono )); studio-theme || { exit_code=1; break; } ;;
        esac ;;
      resize) zdraw resize "$event[rows]" "$event[columns]" nosave || { exit_code=1; break; } ;;
    esac
  done
} always {
  zdraw end || exit_code=1
}
exit "$exit_code"

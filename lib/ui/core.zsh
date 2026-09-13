# SPDX-License-Identifier: LicenseRef-Zsh
# Zsh licence: ../../LICENCE
# Internal helpers reserve _zui_* locals. Public results use documented,
# caller-owned parameters through Zsh dynamic scope. No strings are evaluated.
() {
  builtin emulate -L zsh
  builtin setopt no_aliases
  builtin source "${1:A:h}/color.zsh"
} "${(%):-%x}" || return

# Diagnostics are opt-in and never open a file or take ownership of a descriptor.
# Only the failure path pays for formatting; a broken sink cannot change status.
function _zdraw_ui_error {
  emulate -L zsh
  local _zui_status=$1 _zui_caller=$2 _zui_sink=${ZDRAW_UI_DEBUG_FD-}
  shift 2
  if [[ $_zui_sink == <-> && ${#_zui_sink} -le 5 ]]; then
    ( builtin print -r -u "$_zui_sink" -- "$_zui_caller: $* (status $_zui_status)" ) 2>/dev/null || :
  fi
  return "$_zui_status"
}

function _zdraw_ui_uint {
  emulate -L zsh
  [[ $1 == <-> && ${#1} -le 5 ]] && (( 10#$1 <= 32767 ))
}

function _zdraw_ui_color {
  emulate -L zsh
  local _zui_color=$1
  if [[ $_zui_color == default || $_zui_color == \#[[:xdigit:]][[:xdigit:]][[:xdigit:]][[:xdigit:]][[:xdigit:]][[:xdigit:]] ]]; then
    return 0
  fi
  [[ $_zui_color == <-> && ${#_zui_color} -le 3 ]] && (( 10#$_zui_color <= 255 ))
}

function zdraw-ui-theme {
  emulate -L zsh
  [[ ${(t)zdraw_ui_theme} == (association|association-local) && $# -ge 1 ]] || { _zdraw_ui_error 1 "${(%):-%N}" "requires a writable zdraw_ui_theme association and a theme name"; return $?; }
  local _zui_name=$1 _zui_profile=${2:-${NO_COLOR:+mono}} _zui_pair _zui_key _zui_value
  local -A _zui_theme zdraw_color
  local REPLY
  _zui_profile=${_zui_profile:-16}
  case $_zui_name in
    dark) _zui_theme=(canvas 0 surface 0 text 7 muted 6 accent 6 border 4
                     selection 6 on-selection 0 inactive 4 on-inactive 7 error 1) ;;
    light) _zui_theme=(canvas 7 surface 7 text 0 muted 4 accent 4 border 6
                      selection 4 on-selection 7 inactive 6 on-inactive 0 error 1) ;;
    *) { _zdraw_ui_error 1 "${(%):-%N}" "unknown theme ${(qqq)_zui_name}; expected dark or light"; return $?; } ;;
  esac
  case $_zui_profile in
    16) ;;
    256)
      if [[ $_zui_name == dark ]]; then
        _zui_theme=(canvas 234 surface 236 text 252 muted 245 accent 80 border 240
                    selection 30 on-selection 231 inactive 238 on-inactive 252 error 210)
      else
        _zui_theme=(canvas 255 surface 231 text 235 muted 242 accent 24 border 250
                    selection 24 on-selection 231 inactive 253 on-inactive 235 error 124)
      fi ;;
    auto|rgb)
      if [[ $_zui_name == dark ]]; then
        _zui_theme=(canvas '#121820' surface '#1c2632' text '#e1eaf3' muted '#95a6b8'
          accent '#5edac8' border '#40546b' selection '#255e72' on-selection '#f2fbff'
          inactive '#283747' on-inactive '#acbac9' error '#ff8796')
      else
        _zui_theme=(canvas '#edf2f7' surface '#ffffff' text '#223044' muted '#52677c'
          accent '#086e80' border '#a6b8c8' selection '#145f80' on-selection '#ffffff'
          inactive '#dce5ed' on-inactive '#3e5369' error '#b62948')
      fi ;;
    mono)
      for _zui_key in "${(@k)_zui_theme}"; do _zui_theme[$_zui_key]=default; done ;;
    *) { _zdraw_ui_error 1 "${(%):-%N}" "unknown color profile ${(qqq)_zui_profile}; expected auto, rgb, 16, 256 or mono"; return $?; } ;;
  esac
  shift
  (( $# )) && shift
  (( $# <= 32 )) || { _zdraw_ui_error 1 "${(%):-%N}" "at most 32 theme overrides are allowed"; return $?; }
  for _zui_pair in "$@"; do
    [[ $_zui_pair == *=* ]] || { _zdraw_ui_error 1 "${(%):-%N}" "expected key=color override, got ${(qqq)_zui_pair}"; return $?; }
    _zui_key=${_zui_pair%%=*} _zui_value=${_zui_pair#*=}
    case $_zui_key in
      canvas|surface|text|muted|accent|border|selection|on-selection|inactive|on-inactive|error) ;;
      *) { _zdraw_ui_error 1 "${(%):-%N}" "unknown theme key ${(qqq)_zui_key}"; return $?; } ;;
    esac
    _zdraw_ui_color "$_zui_value" || { _zdraw_ui_error 1 "${(%):-%N}" "invalid color ${(qqq)_zui_value} for ${(qqq)_zui_key}"; return $?; }
    # Canonical spelling keeps numeric aliases from consuming extra color pairs.
    [[ $_zui_value == <-> ]] && _zui_value=$(( 10#$_zui_value ))
    _zui_theme[$_zui_key]=${(L)_zui_value}
  done
  if [[ $_zui_profile == (auto|rgb) ]]; then
    zdraw-color-setup "$_zui_profile" || { _zdraw_ui_error $? "${(%):-%N}" 'color setup requires an initialized, supported session'; return $?; }
    for _zui_key in "${(@k)_zui_theme}"; do
      zdraw-color "$_zui_theme[$_zui_key]" || return
      _zui_theme[$_zui_key]=$REPLY
    done
    _zui_profile=$zdraw_color[profile]
    _zui_theme[color-profile]=$zdraw_color[profile]
    _zui_theme[color-encoding]=$zdraw_color[encoding]
    _zui_theme[rgb-min]=$zdraw_color[rgb_min]
  fi
  _zui_theme[name]=$_zui_name _zui_theme[profile]=$_zui_profile
  zdraw_ui_theme=("${(@kv)_zui_theme}")
}

# Applies one validated property to the resolver's local association.
function _zdraw_ui_property {
  emulate -L zsh
  local _zui_key=${1%%=*} _zui_value=${1#*=}
  case $1 in
    bold|underline|reverse) _zui_resolved[$1]=1; return 0 ;;
    no-bold|no-underline|no-reverse) _zui_resolved[${1#no-}]=0; return 0 ;;
  esac
  [[ $1 == *=* ]] || { _zdraw_ui_error 1 "${(%):-%N}" "expected a style utility, got ${(qqq)1}"; return $?; }
  case $_zui_key in
    fg|bg|border-fg)
      case $_zui_value in
        canvas|surface|text|muted|accent|border|selection|on-selection|inactive|on-inactive|error)
          [[ ${(t)zdraw_ui_theme} == association* ]] || { _zdraw_ui_error 1 "${(%):-%N}" "color tokens require a zdraw_ui_theme association"; return $?; }
          _zui_value=${zdraw_ui_theme[$_zui_value]-} ;;
      esac
      _zdraw_ui_color "$_zui_value" || { _zdraw_ui_error 1 "${(%):-%N}" "invalid color ${(qqq)_zui_value} in ${(qqq)1}"; return $?; }
      [[ $_zui_value == <-> ]] && _zui_value=$(( 10#$_zui_value ))
      _zui_value=${(L)_zui_value}
      if [[ ${(t)zdraw_ui_theme} == association* && -n ${zdraw_ui_theme[color-profile]-} ]]; then
        local -A zdraw_color=(profile "$zdraw_ui_theme[color-profile]"
          encoding "${zdraw_ui_theme[color-encoding]-}" rgb_min "${zdraw_ui_theme[rgb-min]-}")
        local REPLY
        zdraw-color "$_zui_value" || return
        _zui_value=$REPLY
      fi ;;
    px|py)
      [[ $_zui_value == <-> && ${#_zui_value} -le 2 ]] || { _zdraw_ui_error 1 "${(%):-%N}" "padding must be an integer from 0 to 16, got ${(qqq)1}"; return $?; }
      (( 10#$_zui_value <= 16 )) || { _zdraw_ui_error 1 "${(%):-%N}" "padding must be from 0 to 16, got ${(qqq)1}"; return $?; }
      _zui_value=$(( 10#$_zui_value )) ;;
    border) [[ $_zui_value == (none|ascii|rounded|double) ]] || { _zdraw_ui_error 1 "${(%):-%N}" "unknown border ${(qqq)_zui_value}; expected none, ascii, rounded or double"; return $?; } ;;
    align) [[ $_zui_value == (left|center|right) ]] || { _zdraw_ui_error 1 "${(%):-%N}" "unknown alignment ${(qqq)_zui_value}; expected left, center or right"; return $?; } ;;
    *) { _zdraw_ui_error 1 "${(%):-%N}" "unknown style property ${(qqq)_zui_key}"; return $?; } ;;
  esac
  _zui_resolved[$_zui_key]=$_zui_value
}

function zdraw-ui-style {
  emulate -L zsh
  [[ ${(t)zdraw_ui_style} == (association|association-local) && $# -ge 1 && $# -le 129 ]] || { _zdraw_ui_error 1 "${(%):-%N}" "requires a writable zdraw_ui_style association, states and at most 128 utilities"; return $?; }
  local -a _zui_states=("${(@s:,:)1}") _zui_conditions
  local -A _zui_base _zui_variants
  local _zui_token _zui_condition _zui_property _zui_flag _zui_key _zui_style=''
  local -i _zui_match
  local -A _zui_resolved
  for _zui_flag in "${_zui_states[@]}"; do
    [[ $_zui_flag == (normal|focus|selected|inactive|disabled|empty|title|header|alternate|filled|track|label|key|invalid|cursor|heading|subheading|paragraph|bullet|quote|code|separator|spacer|positive|negative|axis|missing|clipped) ]] || { _zdraw_ui_error 1 "${(%):-%N}" "unknown state ${(qqq)_zui_flag}"; return $?; }
  done
  (( ${#_zui_states} )) || { _zdraw_ui_error 1 "${(%):-%N}" "at least one state is required"; return $?; }
  shift
  # Validate even inactive variants. Retain normalized assignments, so each
  # utility is parsed once. Last assignment wins within each precedence tier.
  for _zui_token in "$@"; do
    (( ${#_zui_token} <= 128 )) || { _zdraw_ui_error 1 "${(%):-%N}" "style utility exceeds 128 characters"; return $?; }
    if [[ $_zui_token == *:* ]]; then
      _zui_condition=${_zui_token%%:*} _zui_property=${_zui_token#*:}
      _zui_conditions=("${(@s:+:)_zui_condition}")
      (( ${#_zui_conditions} )) || { _zdraw_ui_error 1 "${(%):-%N}" "empty condition in ${(qqq)_zui_token}"; return $?; }
      _zui_match=1
      for _zui_flag in "${_zui_conditions[@]}"; do
        [[ $_zui_flag == (normal|focus|selected|inactive|disabled|empty|title|header|alternate|filled|track|label|key|invalid|cursor|heading|subheading|paragraph|bullet|quote|code|separator|spacer|positive|negative|axis|missing|clipped) ]] || { _zdraw_ui_error 1 "${(%):-%N}" "unknown condition ${(qqq)_zui_flag} in ${(qqq)_zui_token}"; return $?; }
        (( ${_zui_states[(Ie)$_zui_flag]} )) || _zui_match=0
      done
      _zdraw_ui_property "$_zui_property" || return 1
      _zui_key=${_zui_property%%=*} _zui_key=${_zui_key#no-}
      (( _zui_match )) && _zui_variants[$_zui_key]=$_zui_resolved[$_zui_key]
    else
      _zdraw_ui_property "$_zui_token" || return 1
      _zui_key=${_zui_token%%=*} _zui_key=${_zui_key#no-}
      _zui_base[$_zui_key]=$_zui_resolved[$_zui_key]
    fi
  done
  _zui_resolved=(fg default bg default border-fg default
    border none px 0 py 0 align left bold 0 underline 0 reverse 0)
  _zui_resolved+=("${(@kv)_zui_base}")
  _zui_resolved+=("${(@kv)_zui_variants}")
  for _zui_flag in bold underline reverse; do
    (( _zui_resolved[$_zui_flag] )) && _zui_style+="$_zui_flag,"
  done
  if [[ $_zui_resolved[fg] == default && $_zui_resolved[bg] == default ]]; then
    _zui_style=${_zui_style%,}
  else
    _zui_style+="$_zui_resolved[fg]/$_zui_resolved[bg]"
  fi
  _zui_resolved[style]=$_zui_style
  if [[ $_zui_resolved[border-fg] == default && $_zui_resolved[bg] == default ]]; then
    _zui_resolved[border-style]=''
  else
    _zui_resolved[border-style]="$_zui_resolved[border-fg]/$_zui_resolved[bg]"
  fi
  zdraw_ui_style=("${(@kv)_zui_resolved}")
}

# Validates all geometry before integer assignment or native drawing. The
# caller owns the prefixed integer locals populated here.
function _zdraw_ui_rect {
  emulate -L zsh
  local _zui_number
  local -a _zui_position
  (( $# == 5 )) || { _zdraw_ui_error 1 "${(%):-%N}" "expected window row column height width"; return $?; }
  for _zui_number in "${@:2}"; do _zdraw_ui_uint "$_zui_number" || { _zdraw_ui_error 1 "${(%):-%N}" "geometry must use integers from 0 to 32767, got ${(qqq)_zui_number}"; return $?; }; done
  _zui_y=$((10#$2)) _zui_x=$((10#$3)) _zui_h=$((10#$4)) _zui_w=$((10#$5))
  (( _zui_h > 0 && _zui_w > 0 && _zui_h * _zui_w <= 262144 )) || { _zdraw_ui_error 1 "${(%):-%N}" "drawing rectangle must be nonempty and at most 262144 cells"; return $?; }
  zdraw position "$1" _zui_position || { _zdraw_ui_error $? "${(%):-%N}" 'native position failed'; return $?; }
  (( _zui_y + _zui_h <= _zui_position[5] && _zui_x + _zui_w <= _zui_position[6] )) || {
    _zdraw_ui_error 1 "${(%):-%N}" "rectangle $2 $3 $4 $5 exceeds window ${(qqq)1} ($_zui_position[5] rows, $_zui_position[6] columns)"; return $?
  }
}

# A single clipped/aligned row. Clearing is separate so components can batch
# backgrounds, and all calls retain the native window's cursor and style.
function _zdraw_ui_row {
  emulate -L zsh
  local -A _zui_text
  local -i _zui_column=$3 _zui_width=$4
  (( _zui_width > 0 )) || return 0
  if [[ $5 == left ]]; then
    zdraw spansclip "$1" "$2" "$_zui_column" "$_zui_width" "$7" "$6" || {
      _zdraw_ui_error $? "${(%):-%N}" 'native spansclip failed'; return $?
    }
    return 0
  fi
  zdraw textinfo _zui_text "$6" "$_zui_width" || { _zdraw_ui_error $? "${(%):-%N}" 'native textinfo failed'; return $?; }
  case $5 in
    right) (( _zui_column += _zui_width - _zui_text[width] )) ;;
    center) (( _zui_column += (_zui_width - _zui_text[width]) / 2 )) ;;
  esac
  [[ -n $_zui_text[text] ]] || return 0
  zdraw spansclip "$1" "$2" "$_zui_column" "$_zui_text[width]" "$7" "$_zui_text[text]" || { _zdraw_ui_error $? "${(%):-%N}" 'native spansclip failed'; return $?; }
}

function zdraw-label {
  emulate -L zsh
  (( $# >= 6 )) || { _zdraw_ui_error 1 "${(%):-%N}" "expected window row column width text states [utilities ...]"; return $?; }
  local _zui_win=$1 _zui_label=$5
  local -i _zui_y _zui_x _zui_h _zui_w
  local -A zdraw_ui_style _zui_info
  _zdraw_ui_rect "$1" "$2" "$3" 1 "$4" || return
  (( ${#_zui_label} <= 262144 )) || { _zdraw_ui_error 1 "${(%):-%N}" "label exceeds 262144 characters"; return $?; }
  zdraw textinfo _zui_info "$_zui_label" || { _zdraw_ui_error $? "${(%):-%N}" 'native textinfo failed'; return $?; }
  zdraw-ui-style "$6" fg=text bg=surface "${@:7}" || return
  [[ $zdraw_ui_style[border] == none && $zdraw_ui_style[py] == 0 ]] || { _zdraw_ui_error 1 "${(%):-%N}" "labels require border=none and py=0"; return $?; }
  zdraw fill "$_zui_win" "$_zui_y" "$_zui_x" 1 "$_zui_w" "$zdraw_ui_style[style]" ' ' || { _zdraw_ui_error $? "${(%):-%N}" 'native fill failed'; return $?; }
  _zdraw_ui_row "$_zui_win" "$_zui_y" "$((_zui_x+zdraw_ui_style[px]))" "$((_zui_w-2*zdraw_ui_style[px]))" "$zdraw_ui_style[align]" "$_zui_label" "$zdraw_ui_style[style]"
}

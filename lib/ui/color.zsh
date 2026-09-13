# SPDX-License-Identifier: LicenseRef-Zsh
# Zsh licence: ../../LICENCE
# Caller-owned zdraw_color is a capability snapshot. _zdc_* names are internal.
function zdraw-color-setup {
  emulate -L zsh
  [[ ${(t)zdraw_color} == (association|association-local) && $# -le 1 ]] || return 1
  local _zdc_profile=${1-auto} _zdc_encoding=indexed
  local -A _zdc_info
  [[ $_zdc_profile == (auto|rgb|256|16|mono) ]] || return 1
  zdraw colorinfo _zdc_info || return
  [[ $_zdc_info[initialized] == 1 ]] || return 1
  if [[ $_zdc_profile == auto ]]; then
    if [[ -n ${NO_COLOR-} ]]; then
      _zdc_profile=mono
    elif [[ $_zdc_info[truecolor_supported] == 1 ]]; then
      _zdc_profile=rgb
    elif [[ $_zdc_info[color_started] == 1 && $_zdc_info[colors] == <-> ]] &&
         (( _zdc_info[colors] >= 256 && _zdc_info[colors] < 16777216 )); then
      _zdc_profile=256
    elif [[ $_zdc_info[color_started] == 1 ]] && (( _zdc_info[color_limit] >= 7 )); then
      _zdc_profile=16
    else
      _zdc_profile=mono
    fi
  fi
  case $_zdc_profile in
    rgb) [[ $_zdc_info[truecolor_supported] == 1 ]] || return 2 ;;
    256)
      [[ $_zdc_info[color_started] == 1 ]] && (( _zdc_info[color_limit] >= 255 )) || return 2
      # Large direct-color counts do not describe an indexed palette.
      if (( _zdc_info[colors] >= 16777216 )); then
        [[ $_zdc_info[truecolor_supported] == 1 ]] || return 2
      fi ;;
    16) [[ $_zdc_info[color_started] == 1 ]] && (( _zdc_info[color_limit] >= 7 )) || return 2 ;;
  esac
  if [[ $_zdc_profile != mono && $_zdc_info[truecolor_supported] == 1 ]]; then
    zdraw truecolor on || return
    _zdc_encoding=rgb
  fi
  zdraw_color=(profile "$_zdc_profile" encoding "$_zdc_encoding"
    rgb_min "${_zdc_info[rgb_min]}")
}

# Parse a named/indexed/hex color into a packed RGB value or palette index.
# RGB approximations of indices 0-15 use the conventional xterm palette;
# explicit basic indices remain terminal-defined when they can be retained.
function _zdraw_color_parse {
  emulate -L zsh
  local _zdc_input=${(L)1}
  local -a _zdc_basic=(000000 800000 008000 808000 000080 800080 008080 c0c0c0
                      808080 ff0000 00ff00 ffff00 0000ff ff00ff 00ffff ffffff)
  local -a _zdc_levels=(0 95 135 175 215 255)
  _zdc_index=-1
  case $_zdc_input in
    default) _zdc_index=-2; return 0 ;;
    black) _zdc_input=0 ;; red) _zdc_input=1 ;; green) _zdc_input=2 ;;
    yellow) _zdc_input=3 ;; blue) _zdc_input=4 ;; magenta) _zdc_input=5 ;;
    cyan) _zdc_input=6 ;; white) _zdc_input=7 ;;
  esac
  if [[ $_zdc_input == \#[[:xdigit:]][[:xdigit:]][[:xdigit:]][[:xdigit:]][[:xdigit:]][[:xdigit:]] ]]; then
    _zdc_rgb=$((16#${_zdc_input#\#}))
  elif [[ $_zdc_input == <-> && ${#_zdc_input} -le 3 ]] && (( 10#$_zdc_input <= 255 )); then
    _zdc_index=$((10#$_zdc_input))
    if (( _zdc_index < 16 )); then
      _zdc_rgb=$((16#${_zdc_basic[_zdc_index+1]}))
    elif (( _zdc_index < 232 )); then
      local -i _zdc_cube=$((_zdc_index-16))
      _zdc_rgb=$((_zdc_levels[_zdc_cube/36+1]*65536 +
        _zdc_levels[_zdc_cube/6%6+1]*256 + _zdc_levels[_zdc_cube%6+1]))
    else
      _zdc_rgb=$(((_zdc_index-232)*10+8))
      (( _zdc_rgb *= 65793 ))
    fi
  else
    return 1
  fi
  return 0
}

# Nearest squared RGB distance, considering the entire cube AND gray ramp.
# This deliberately excludes mutable ANSI slots in the 256-color approximation.
function _zdraw_color_quantize {
  emulate -L zsh
  local -i _zdc_r=$((_zdc_rgb>>16)) _zdc_g=$((_zdc_rgb>>8&255)) _zdc_b=$((_zdc_rgb&255))
  local -i _zdc_i _zdc_d _zdc_best=2147483647 _zdc_gray
  if [[ $1 == 16 ]]; then
    for (( _zdc_i=0; _zdc_i<8; ++_zdc_i )); do
      # Eight portable ANSI hues; the terminal controls their actual appearance.
      (( _zdc_d=(_zdc_r-((_zdc_i&1)?192:0))**2 +
                 (_zdc_g-((_zdc_i&2)?192:0))**2 +
                 (_zdc_b-((_zdc_i&4)?192:0))**2 ))
      if (( _zdc_d < _zdc_best )); then
        _zdc_best=$_zdc_d _zdc_index=$_zdc_i
      fi
    done
  else
    local -a _zdc_levels=(0 95 135 175 215 255) _zdc_nearest=()
    local -i _zdc_channel _zdc_pick
    for _zdc_channel in $_zdc_r $_zdc_g $_zdc_b; do
      _zdc_best=2147483647 _zdc_pick=0
      for (( _zdc_i=0; _zdc_i<6; ++_zdc_i )); do
        (( _zdc_d=(_zdc_channel-_zdc_levels[_zdc_i+1])**2 ))
        if (( _zdc_d < _zdc_best )); then
          _zdc_best=$_zdc_d _zdc_pick=$_zdc_i
        fi
      done
      _zdc_nearest+=($_zdc_pick)
    done
    (( _zdc_index=16+36*_zdc_nearest[1]+6*_zdc_nearest[2]+_zdc_nearest[3],
       _zdc_best=(_zdc_r-_zdc_levels[_zdc_nearest[1]+1])**2 +
                 (_zdc_g-_zdc_levels[_zdc_nearest[2]+1])**2 +
                 (_zdc_b-_zdc_levels[_zdc_nearest[3]+1])**2 ))
    for (( _zdc_i=0; _zdc_i<24; ++_zdc_i )); do
      (( _zdc_gray=8+10*_zdc_i,
         _zdc_d=(_zdc_r-_zdc_gray)**2+(_zdc_g-_zdc_gray)**2+(_zdc_b-_zdc_gray)**2 ))
      if (( _zdc_d < _zdc_best )); then
        _zdc_best=$_zdc_d _zdc_index=$((_zdc_i+232))
      fi
    done
  fi
  return 0
}

function zdraw-color {
  emulate -L zsh
  [[ $# == 1 && ${(t)REPLY} == (scalar|scalar-local) &&
     ${(t)zdraw_color} == association* ]] || return 1
  local _zdc_profile=${zdraw_color[profile]-} _zdc_encoding=${zdraw_color[encoding]-}
  local _zdc_min=${zdraw_color[rgb_min]-} _zdc_result
  local -i _zdc_rgb=0 _zdc_index
  [[ $_zdc_profile == (rgb|256|16|mono) && $_zdc_encoding == (rgb|indexed) ]] || return 1
  if [[ $_zdc_encoding == rgb ]]; then
    [[ $_zdc_min == <-> && ${#_zdc_min} -le 8 ]] && (( 10#$_zdc_min <= 16777215 )) || return 1
  elif [[ $_zdc_profile == rgb ]]; then
    return 1
  fi
  _zdraw_color_parse "$1" || return
  if (( _zdc_index == -2 )) || [[ $_zdc_profile == mono ]]; then
    REPLY=default
    return 0
  fi
  case $_zdc_profile in
    16) (( _zdc_index >= 0 && _zdc_index < 8 )) || _zdraw_color_quantize 16 ;;
    256) (( _zdc_index >= 0 )) || _zdraw_color_quantize 256 ;;
  esac
  if [[ $_zdc_encoding == rgb ]]; then
    if (( _zdc_index >= 8 || (_zdc_index >= 0 && _zdc_index >= 10#$_zdc_min) )); then
      _zdraw_color_parse "$_zdc_index" || return
      _zdc_index=-1
    fi
    if (( _zdc_index < 0 )); then
      (( _zdc_rgb < 10#$_zdc_min )) && _zdc_rgb=$((10#$_zdc_min))
      builtin printf -v _zdc_result '#%06x' "$_zdc_rgb"
    else
      _zdc_result=$_zdc_index
    fi
  else
    _zdc_result=$_zdc_index
  fi
  REPLY=$_zdc_result
}

# Precompute a bounded ramp once, then reuse it during redraws.
function zdraw-color-gradient {
  emulate -L zsh
  [[ $# == 3 && ${(t)reply} == (array|array-local) && $3 == <-> && ${#3} -le 3 ]] || return 1
  local -i _zdc_count=$((10#$3)) _zdc_rgb _zdc_index _zdc_start _zdc_end _zdc_step _zdc_shift _zdc_value
  (( _zdc_count >= 2 && _zdc_count <= 256 )) || return 1
  _zdraw_color_parse "$1" && (( _zdc_index != -2 )) || return 1
  _zdc_start=$_zdc_rgb
  _zdraw_color_parse "$2" && (( _zdc_index != -2 )) || return 1
  _zdc_end=$_zdc_rgb
  local REPLY _zdc_hex
  local -a _zdc_ramp=()
  for (( _zdc_step=0; _zdc_step<_zdc_count; ++_zdc_step )); do
    _zdc_value=0
    for _zdc_shift in 16 8 0; do
      (( _zdc_value |= ((((_zdc_start>>_zdc_shift)&255)*(_zdc_count-1-_zdc_step) +
                         ((_zdc_end>>_zdc_shift)&255)*_zdc_step + (_zdc_count-1)/2) /
                         (_zdc_count-1)) << _zdc_shift ))
    done
    builtin printf -v _zdc_hex '#%06x' "$_zdc_value"
    zdraw-color "$_zdc_hex" || return
    _zdc_ramp+=("$REPLY")
  done
  reply=("${_zdc_ramp[@]}")
}

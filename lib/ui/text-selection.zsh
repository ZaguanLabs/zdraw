# SPDX-License-Identifier: LicenseRef-Zsh
# Zsh licence: ../../LICENCE
# _zts_* locals are reserved; caller associations use intentional dynamic scope.
function _zdraw_ts_error {
  print -ru2 -- "zdraw text selection: $*"
  return 1
}
function _zdraw_ts_uint {
  [[ $1 == <-> && ${#1} -le 8 ]] && (( 10#$1 <= 16777216 ))
}
function _zdraw_ts_state {
  [[ ${(t)zdraw_text_selection} == (association|association-local) &&
     ${zdraw_text_selection[format]-} == zdraw-text-selection-experiment-1 ]] ||
    _zdraw_ts_error 'requires initialized writable zdraw_text_selection'
}
# Byte slicing is isolated from the locale used for native text measurements.
function _zdraw_ts_slice {
  emulate -L zsh
  local LC_ALL=C
  REPLY=''
  (( $3 > $2 )) && REPLY=${1[$2+1,$3]}
  return 0
}
function _zdraw_ts_bytes {
  emulate -L zsh
  local LC_ALL=C
  REPLY=${#1}
}

# text revision screen-y screen-x height width policy [row column byte style text ...]
function zdraw-text-selection-init {
  emulate -L zsh
  [[ $# -ge 7 && $(( ($# - 7) % 5 )) == 0 &&
     ${(t)zdraw_text_selection} == (association|association-local) ]] ||
    { _zdraw_ts_error 'expected text revision y x height width policy and row/column/byte/style/text records'; return 1; }
  local _zts_v REPLY _zts_source=$1 _zts_revision=$2 _zts_policy=$7
  for _zts_v in "$3" "$4" "$5" "$6"; do
    _zdraw_ts_uint "$_zts_v" || { _zdraw_ts_error 'invalid geometry'; return 1; }
  done
  local -i _zts_y=$((10#$3)) _zts_x=$((10#$4)) _zts_h=$((10#$5)) _zts_w=$((10#$6))
  (( _zts_h > 0 && _zts_h <= 4096 && _zts_w > 0 && _zts_w <= 4096 &&
     _zts_y+_zts_h <= 32767 && _zts_x+_zts_w <= 32767 )) ||
    { _zdraw_ts_error 'rectangle must fit 32767 cells; dimensions at most 4096'; return 1; }
  local -a _zts_policy_arg
  [[ $_zts_policy == native ]] || _zts_policy_arg=("$_zts_policy")
  [[ $_zts_policy == (native|unicode-17.0.0-egc-wcwidth-sum-attach-zero) ]] ||
    { _zdraw_ts_error 'policy must be native or the shared grapheme-safe drawing policy'; return 1; }
  local -A _zts_info _zts_hit _zts_build=(format zdraw-text-selection-experiment-1
    source "$_zts_source" revision "$_zts_revision" y $_zts_y x $_zts_x height $_zts_h width $_zts_w
    policy "$_zts_policy" valid 1 active 0 draining 0 selected 0 start 0 end 0 consumed 0 changed 1)
  _zdraw_ts_bytes "$_zts_source"
  local -i _zts_bytes=$REPLY
  (( _zts_bytes <= 16777216 )) || { _zdraw_ts_error 'source exceeds 16 MiB'; return 1; }
  # Validate the canonical copy text independently of which lines are visible.
  local _zts_line
  for _zts_line in "${(@ps:\n:)_zts_source}"; do
    zdraw textinfo _zts_info "$_zts_line" 2147483647 "${_zts_policy_arg[@]}" || return $?
  done
  shift 7
  local -i _zts_r _zts_c _zts_b _zts_n _zts_i _zts_j _zts_last_r=-1 _zts_last_b=0
  local _zts_style _zts_text _zts_prefix _zts_suffix
  while (( $# )); do
    for _zts_v in "$1" "$2" "$3"; do
      _zdraw_ts_uint "$_zts_v" || { _zdraw_ts_error 'invalid row mapping'; return 1; }
    done
    _zts_r=$((10#$1)) _zts_c=$((10#$2)) _zts_b=$((10#$3)) _zts_style=$4 _zts_text=$5
    (( _zts_r < _zts_h && _zts_c < _zts_w && _zts_r >= _zts_last_r && _zts_b >= _zts_last_b )) ||
      { _zdraw_ts_error 'mappings must be in screen and source order'; return 1; }
    _zdraw_ts_bytes "$_zts_text"; _zts_n=$REPLY
    (( _zts_b+_zts_n <= _zts_bytes )) || { _zdraw_ts_error 'mapping exceeds source'; return 1; }
    _zdraw_ts_slice "$_zts_source" $_zts_b $((_zts_b+_zts_n))
    [[ $REPLY == "$_zts_text" && $_zts_text != *$'\n'* ]] ||
      { _zdraw_ts_error 'display text must match a single-line source slice'; return 1; }
    if [[ -v "_zts_build[$_zts_r,text]" ]]; then
      # Adjacent styled fragments form one row before any Unicode segmentation.
      (( _zts_b == _zts_last_b && _zts_c == _zts_build[$_zts_r,column] )) ||
        { _zdraw_ts_error 'row fragments require contiguous source and the same row origin'; return 1; }
    else
      if (( _zts_last_r >= 0 )); then
        _zdraw_ts_slice "$_zts_source" $_zts_last_b $_zts_b
        [[ ${REPLY//$'\n'/} == '' ]] ||
          { _zdraw_ts_error 'only real line breaks may be omitted between mapped rows'; return 1; }
      fi
      _zts_build[$_zts_r,text]='' _zts_build[$_zts_r,column]=$_zts_c _zts_build[$_zts_r,start]=$_zts_b
    fi
    _zts_build[$_zts_r,text]+=$_zts_text
    for (( _zts_i=_zts_b; _zts_i<_zts_b+_zts_n; _zts_i++ )); do
      _zts_build[s,$_zts_i]=$_zts_style
    done
    _zts_build[$_zts_r,end]=$((_zts_b+_zts_n))
    _zts_last_r=$_zts_r _zts_last_b=$((_zts_b+_zts_n))
    shift 5
  done
  local -i _zts_u=0 _zts_offset _zts_origin _zts_start _zts_end _zts_width _zts_nearest=-1
  for (( _zts_r=0; _zts_r<_zts_h; _zts_r++ )); do
    [[ -v "_zts_build[$_zts_r,text]" ]] || continue
    _zts_text=$_zts_build[$_zts_r,text] _zts_b=$_zts_build[$_zts_r,start]
    # Verify row edges against the original logical line, including units split
    # across a wrap. A valid standalone suffix alone cannot prove this.
    _zdraw_ts_slice "$_zts_source" 0 $_zts_b
    _zts_prefix=${REPLY##*$'\n'}
    _zdraw_ts_bytes "$_zts_prefix"; _zts_origin=$REPLY
    _zdraw_ts_slice "$_zts_source" $_zts_b $_zts_bytes
    _zts_suffix=${REPLY%%$'\n'*}
    _zts_line=$_zts_prefix$_zts_suffix
    for _zts_offset in $_zts_origin $((_zts_origin+_zts_build[$_zts_r,end]-_zts_b)); do
      zdraw textpos _zts_hit "$_zts_line" byte $_zts_offset "${_zts_policy_arg[@]}" || return $?
      (( _zts_hit[byte_start] == _zts_offset )) || { _zdraw_ts_error 'row edge splits a text unit'; return 1; }
    done
    zdraw textinfo _zts_info "$_zts_text" 2147483647 "${_zts_policy_arg[@]}" || return $?
    _zts_width=$_zts_info[width] _zts_c=$_zts_build[$_zts_r,column]
    (( _zts_c+_zts_width <= _zts_w )) || { _zdraw_ts_error 'row exceeds content rectangle'; return 1; }
    _zts_build[$_zts_r,width]=$_zts_width _zts_build[$_zts_r,first]=$((_zts_u+1))
    _zts_offset=0
    while (( _zts_offset < _zts_width )); do
      zdraw textpos _zts_hit "$_zts_text" column $_zts_offset "${_zts_policy_arg[@]}" || return $?
      (( ++_zts_u ))
      _zts_start=$((_zts_b+_zts_hit[byte_start])) _zts_end=$((_zts_b+_zts_hit[byte_end]))
      _zts_build[u,$_zts_u,start]=$_zts_start _zts_build[u,$_zts_u,end]=$_zts_end
      _zts_build[u,$_zts_u,text]=$_zts_hit[text] _zts_build[u,$_zts_u,style]=${_zts_build[s,$_zts_start]-}
      for (( _zts_j=_zts_offset; _zts_j<_zts_hit[column_end]; _zts_j++ )); do
        _zts_build[$_zts_r,c,$((_zts_c+_zts_j))]=$_zts_u
      done
      _zts_offset=$_zts_hit[column_end]
    done
    _zts_build[$_zts_r,last]=$_zts_u
  done
  # Unmapped rows during a drag resolve to the preceding end, or first start.
  for (( _zts_r=_zts_h-1; _zts_r>=0; _zts_r-- )); do
    [[ -v "_zts_build[$_zts_r,start]" ]] && _zts_nearest=$_zts_build[$_zts_r,start]
    _zts_build[$_zts_r,blank]=$_zts_nearest
  done
  _zts_nearest=-1
  for (( _zts_r=0; _zts_r<_zts_h; _zts_r++ )); do
    (( _zts_nearest >= 0 )) && _zts_build[$_zts_r,blank]=$_zts_nearest
    [[ -v "_zts_build[$_zts_r,end]" ]] && _zts_nearest=$_zts_build[$_zts_r,end]
  done
  # Temporary per-byte styles aren't needed on the drag path.
  for _zts_v in "${(@k)_zts_build}"; do
    [[ $_zts_v == s,* ]] && unset "_zts_build[$_zts_v]"
  done
  # Replacement also drains a still-held gesture; it cannot select new content.
  _zts_build[draining]=${zdraw_text_selection[draining]:-0}
  [[ ${zdraw_text_selection[active]-0} == 1 ]] && _zts_build[draining]=1
  zdraw_text_selection=("${(@kv)_zts_build}")
  return 0
}

function zdraw-text-selection-clear {
  emulate -L zsh
  _zdraw_ts_state || return
  zdraw_text_selection[changed]=$(( zdraw_text_selection[selected] || zdraw_text_selection[active] ))
  (( zdraw_text_selection[active] )) && zdraw_text_selection[draining]=1
  zdraw_text_selection[active]=0 zdraw_text_selection[selected]=0
  zdraw_text_selection[start]=0 zdraw_text_selection[end]=0
  return 0
}
function zdraw-text-selection-invalidate {
  emulate -L zsh
  zdraw-text-selection-clear || return
  zdraw_text_selection[valid]=0
}

# Call only after the application knows the old gesture has ended (for example
# after regaining ownership with no held button). Lost releases cannot be inferred.
function zdraw-text-selection-reset {
  emulate -L zsh
  zdraw-text-selection-clear || return
  zdraw_text_selection[draining]=0
}

# revision; reads caller-owned zdraw_selection_event. Success != consumed.
function zdraw-text-selection-event {
  emulate -L zsh
  _zdraw_ts_state || return
  [[ $# == 1 && ${(t)zdraw_selection_event} == association* ]] ||
    { _zdraw_ts_error 'event requires revision and zdraw_selection_event association'; return 1; }
  zdraw_text_selection[consumed]=0 zdraw_text_selection[changed]=0
  if [[ $1 != "$zdraw_text_selection[revision]" || ${zdraw_selection_event[type]-} == resize ||
        ( ${zdraw_selection_event[type]-} == focus && ${zdraw_selection_event[focused]-} == 0 ) ]]; then
    zdraw-text-selection-invalidate || return
  fi
  if [[ ${zdraw_selection_event[type]-} == character && ${zdraw_selection_event[text]-} == $'\e' ||
        ${zdraw_selection_event[type]-} == key && ${zdraw_selection_event[key]-} == ESC ]]; then
    if (( zdraw_text_selection[selected] || zdraw_text_selection[active] )); then
      zdraw-text-selection-clear || return
      zdraw_text_selection[consumed]=1
    fi
    return 0
  fi
  [[ ${zdraw_selection_event[type]-} == mouse ]] || return 0
  local _zts_buttons=" ${zdraw_selection_event[buttons]-} "
  local -i _zts_press=0 _zts_release=0 _zts_r _zts_c _zts_u=0 _zts_start _zts_end
  [[ $_zts_buttons == *' PRESSED1 '* ]] && _zts_press=1
  [[ $_zts_buttons == *' RELEASED1 '* ]] && _zts_release=1
  if (( zdraw_text_selection[draining] )); then
    # Do not interpret repeated button states during motion as a new gesture.
    zdraw_text_selection[consumed]=1
    (( _zts_release )) && zdraw_text_selection[draining]=0
    return 0
  fi
  (( zdraw_text_selection[valid] )) || return 0
  # An active drag owns mouse events even when they carry other button states.
  (( zdraw_text_selection[active] )) && zdraw_text_selection[consumed]=1
  if (( ! zdraw_text_selection[active] && ! _zts_press )); then return 0; fi
  local _zts_coord
  for _zts_coord in "${zdraw_selection_event[x]-}" "${zdraw_selection_event[y]-}"; do
    [[ $_zts_coord == -<-> || $_zts_coord == <-> ]] && (( ${#_zts_coord} <= 8 )) ||
      { _zdraw_ts_error 'invalid mouse coordinate'; return 1; }
  done
  # Native event coordinates are canonical decimal integers.
  _zts_r=$(( zdraw_selection_event[y]-zdraw_text_selection[y] ))
  _zts_c=$(( zdraw_selection_event[x]-zdraw_text_selection[x] ))
  if (( ! zdraw_text_selection[active] )); then
    [[ ${zdraw_selection_event[modifiers]-} == '' ]] || return 0
    _zts_u=${zdraw_text_selection[$_zts_r,c,$_zts_c]:-0}
    (( _zts_u )) || return 0
    zdraw_text_selection[anchor_start]=$zdraw_text_selection[u,$_zts_u,start]
    zdraw_text_selection[anchor_end]=$zdraw_text_selection[u,$_zts_u,end]
    zdraw_text_selection[active]=1 zdraw_text_selection[consumed]=1
  fi
  (( _zts_r < 0 )) && _zts_r=0
  (( _zts_r >= zdraw_text_selection[height] )) && _zts_r=$((zdraw_text_selection[height]-1))
  _zts_u=${zdraw_text_selection[$_zts_r,c,$_zts_c]:-0}
  if (( _zts_u )); then
    _zts_start=$zdraw_text_selection[u,$_zts_u,start] _zts_end=$zdraw_text_selection[u,$_zts_u,end]
  elif [[ -v "zdraw_text_selection[$_zts_r,start]" ]]; then
    if (( _zts_c < zdraw_text_selection[$_zts_r,column] )); then
      _zts_start=$zdraw_text_selection[$_zts_r,start]
    else
      _zts_start=$zdraw_text_selection[$_zts_r,end]
    fi
    _zts_end=$_zts_start
  else
    _zts_start=$zdraw_text_selection[$_zts_r,blank] _zts_end=$_zts_start
  fi
  (( _zts_start < 0 )) && return 0
  (( _zts_start > zdraw_text_selection[anchor_start] )) && _zts_start=$zdraw_text_selection[anchor_start]
  (( _zts_end < zdraw_text_selection[anchor_end] )) && _zts_end=$zdraw_text_selection[anchor_end]
  (( _zts_start != zdraw_text_selection[start] || _zts_end != zdraw_text_selection[end] || ! zdraw_text_selection[selected] )) &&
    zdraw_text_selection[changed]=1
  zdraw_text_selection[start]=$_zts_start zdraw_text_selection[end]=$_zts_end zdraw_text_selection[selected]=1
  (( _zts_release )) && zdraw_text_selection[active]=0
  return 0
}

# Returns exact bytes through REPLY, including trailing newlines.
function zdraw-text-selection-get {
  emulate -L zsh
  _zdraw_ts_state || return
  REPLY=''
  (( zdraw_text_selection[valid] && zdraw_text_selection[selected] )) || return 0
  _zdraw_ts_slice "$zdraw_text_selection[source]" "$zdraw_text_selection[start]" "$zdraw_text_selection[end]"
}

# row [selection-attributes]; returns style/text pairs in reply. The default
# toggles reverse/standout while keeping the other original attributes.
function zdraw-text-selection-spans {
  emulate -L zsh
  _zdraw_ts_state || return
  [[ $# -ge 1 && $# -le 2 ]] && _zdraw_ts_uint "$1" || return 1
  local -i _zts_r=$((10#$1)) _zts_u _zts_first _zts_last
  (( _zts_r < zdraw_text_selection[height] )) || return 1
  local _zts_style _zts_previous='' _zts_text='' _zts_highlight=${2:-reverse}
  local -a _zts_attrs
  [[ $_zts_highlight == (standout|reverse|underline|bold) ]] || return 1
  reply=()
  _zts_first=${zdraw_text_selection[$_zts_r,first]:-1} _zts_last=${zdraw_text_selection[$_zts_r,last]:-0}
  for (( _zts_u=_zts_first; _zts_u<=_zts_last; _zts_u++ )); do
    _zts_style=$zdraw_text_selection[u,$_zts_u,style]
    if (( zdraw_text_selection[selected] && zdraw_text_selection[u,$_zts_u,start] < zdraw_text_selection[end] &&
          zdraw_text_selection[u,$_zts_u,end] > zdraw_text_selection[start] )); then
      _zts_attrs=("${(@s:,:)_zts_style}")
      if [[ $_zts_highlight == reverse ]] && (( ${_zts_attrs[(Ie)reverse]} || ${_zts_attrs[(Ie)standout]} )); then
        _zts_attrs=("${(@)_zts_attrs:#reverse}")
        _zts_attrs=("${(@)_zts_attrs:#standout}")
        _zts_style=${(j:,:)_zts_attrs}
      else
        [[ -n $_zts_style ]] && _zts_style+=,
        _zts_style+=$_zts_highlight
      fi
    fi
    if [[ $_zts_style != "$_zts_previous" && -n $_zts_text ]]; then
      reply+=("$_zts_previous" "$_zts_text")
      _zts_text=''
    fi
    _zts_previous=$_zts_style _zts_text+=$zdraw_text_selection[u,$_zts_u,text]
  done
  [[ -n $_zts_text ]] && reply+=("$_zts_previous" "$_zts_text")
  return 0
}

# Captured Alpine Vigil v0.1.2 presenter, 2026-09-27.
# Only clock substitution: SECONDS instead of the optional datetime module.
# Prepared half-block rows: RGB when supported, bounded indexed fallback otherwise.
typeset -gi screen_rows screen_cols view_w view_h left top prepared=0 show_info=0 coarse=1
typeset -g color_mode=indexed
typeset -gA view_styles=() view_nearest_color=()
typeset -gi view_force_budget=0 view_fallback_cells=0
typeset -gA view_colorplan=()
typeset -F cell_aspect=2.0 build_ms=0 prepare_ms=0 present_ms=0
view-colors() {
  local -A info
  zdraw colorinfo info || return
  if [[ -n ${NO_COLOR:-} || $info[has_colors] != 1 || $requested_color == mono ]]; then
    color_mode=mono
  elif [[ $requested_color != indexed && $info[truecolor_supported] == 1 ]]; then
    zdraw truecolor on || return
    color_mode=rgb
  elif [[ $requested_color == rgb ]]; then
    print -ru2 -- 'RGB requires a compatible direct-color terminfo entry (for example xterm-direct).'
    return 1
  elif ((info[color_limit]>=255)); then color_mode=indexed
  else color_mode=basic
  fi
  if [[ $color_mode == indexed ]] && (( ${#indexed}<${#rgb} )); then paint-indexed-palette || return; fi
  [[ ${LC_ALL:-${LC_CTYPE:-$LANG}} == *([uU][tT][fF]-8|[uU][tT][fF]8)* ]] || color_mode=mono
}
view-prepare() {
  local -a dimensions spans upper_pixels lower_pixels pending_spans row_starts row_ends frame_pairs
  local -A frame_seen=()
  local -i budget=${1:-$view_force_budget} offset stop fallback_cells=0
  local background='black/black'
  [[ $color_mode != mono ]] || background='' 
  local -i y x sx sy upper lower last_upper=-1 last_lower=-1 row old=$prepared pixel_index depth
  local -F began=$SECONDS
  local style run glyph fg bg
  local -a basic=(0 0 4 4 4 4 5 5 5 1 1 4 6 7 0 3 3 3 0 0 1 3 3 3 3 7 7 0 6 0 7 7)
  local -a luminance=(0 1 1 2 3 4 4 4 5 6 7 4 5 6 3 4 5 6 1 2 2 3 4 5 7 8 9 1 2 3 5 7)
  local -i palette_index red green blue brightness nearest distance best
  local hex
  local -a ansi_r=(0 170 0 170 0 170 0 170) ansi_g=(0 0 170 85 0 0 170 170) ansi_b=(0 0 0 0 170 170 170 170)
  if [[ $color_mode == mono || $color_mode == basic ]]; then
  for ((palette_index=1;palette_index<=${#rgb};palette_index++)); do
    hex=$rgb[palette_index]
    ((red=16#${hex[1,2]},green=16#${hex[3,4]},blue=16#${hex[5,6]},brightness=(red*3+green*6+blue)/10,best=200000))
    luminance[palette_index]=$((brightness*9/255))
    for ((nearest=1;nearest<=8;nearest++)); do
      ((distance=(red-ansi_r[nearest])**2+(green-ansi_g[nearest])**2+(blue-ansi_b[nearest])**2))
      if ((distance<best)); then best=$distance basic[palette_index]=$((nearest-1)); fi
    done
  done
  fi
  local ramp=' .,:;ox%#@'
  zdraw position stdscr dimensions || return
  screen_rows=$dimensions[5] screen_cols=$dimensions[6]
  # Correct the artwork's physical aspect ratio using the chosen font cell ratio.
  ((view_w=screen_cols,view_h=int(view_w*396.0/750.0/cell_aspect)))
  if ((view_h>screen_rows-show_info)); then
    ((view_h=screen_rows-show_info,view_w=int(view_h*cell_aspect*750.0/396.0)))
  fi
  ((view_w=view_w<1?1:view_w,view_h=view_h<1?1:view_h,left=(screen_cols-view_w)/2,top=(screen_rows-show_info-view_h)/2,top=top<0?0:top))
  for ((y=0;y<view_h;y++)); do
    ((sy=(y*art_h/view_h)/coarse*coarse))
    upper_pixels=("${(@)pixels[sy*art_w*2+1,(sy+1)*art_w*2]}")
    ((sy=((y*2+1)*art_h/(view_h*2))/coarse*coarse))
    lower_pixels=("${(@)pixels[sy*art_w*2+1,(sy+1)*art_w*2]}")
    spans=() run='' last_upper=-1 last_lower=-1
    for ((x=0;x<view_w;x++)); do
      ((sx=(x*art_w/view_w)/coarse*coarse,pixel_index=sx*2+1,
        upper=upper_pixels[pixel_index+1]>0?int(upper_pixels[pixel_index+1]*256+0.5)%256:upper_pixels[pixel_index],
        lower=lower_pixels[pixel_index+1]>0?int(lower_pixels[pixel_index+1]*256+0.5)%256:lower_pixels[pixel_index]))
      if ((upper!=last_upper || lower!=last_lower)); then
        [[ -z $run ]] || spans+=("$style" "$run")
        case $color_mode in
          rgb) fg=\#$rgb[upper+1] bg=\#$rgb[lower+1] ;;
          indexed) fg=$indexed[upper+1] bg=$indexed[lower+1] ;;
          basic) fg=$basic[upper+1] bg=$basic[lower+1] ;;
        esac
        if [[ $color_mode == mono ]]; then
          style='' glyph=$ramp[luminance[upper+1]+1]
        else
          style="$fg/$bg" glyph='▀'
          if [[ $color_mode == rgb && -z ${view_styles[$style]-} ]] && ((budget)); then
            view-budget-color $fg; fg=$REPLY
            view-budget-color $bg; bg=$REPLY
            style="$fg/$bg"
            ((fallback_cells++))
          fi
          if [[ -z ${frame_seen[$style]-} ]]; then
            frame_seen[$style]=1
            frame_pairs+=("$style")
          fi
          ((upper!=lower)) || glyph=' '
        fi
        last_upper=$upper last_lower=$lower run=$glyph
      else run+=$glyph
      fi
    done
    [[ -z $run ]] || spans+=("$style" "$run")
    row_starts+=($((${#pending_spans}+1)))
    pending_spans+=("${spans[@]}")
    row_ends+=(${#pending_spans})
  done
  # The fill follows preparation, so plan its pair last in allocation order.
  [[ -z $background || -n ${frame_seen[$background]-} ]] || frame_pairs+=("$background")
  if ((${#frame_pairs}>65536)); then
    ((budget)) && { print -ru2 -- 'Frame exceeds the colorplan request bound.'; return 1; }
    view-prepare 1 || return
    ((prepare_ms=(SECONDS-began)*1000))
    return 0
  fi
  zdraw colorplan view_colorplan spans "${frame_pairs[@]}" || return
  # Keep space for the 64² pigment fallback in later layouts. Native preflight
  # accounts for real session allocations, including colors owned by other code.
  if (( !view_colorplan[fits] || (!budget && view_colorplan[pairs_needed]>0 && view_colorplan[max_pair]>view_colorplan[path_pair_limit]-4097) )); then
    if ((budget)); then
      print -ru2 -- 'The frame cannot fit the remaining native color-pair budget.'
      return 1
    fi
    view-prepare 1 || return
    ((prepare_ms=(SECONDS-began)*1000))
    return 0
  fi
  # No old rows or pairs are changed until the complete frame passes preflight.
  for ((row=0;row<old;row++)); do zdraw unprepare art_$row || return; done
  prepared=0
  for ((y=0;y<view_h;y++)); do
    ((offset=row_starts[y+1],stop=row_ends[y+1]))
    zdraw prepare art_$y "${(@)pending_spans[offset,stop]}" || return
    ((prepared++))
  done
  for style in $frame_pairs; do view_styles[$style]=1; done
  ((view_fallback_cells+=fallback_cells))
  ((prepare_ms=(SECONDS-began)*1000))
  return 0
}
view-present() {
  local -i y
  local -F began=$SECONDS
  local label
  local background='black/black'
  [[ $color_mode != mono ]] || background=''
  zdraw fill stdscr 0 0 $screen_rows $screen_cols "$background" ' ' || return
  for ((y=0;y<prepared;y++)); do
    zdraw draw stdscr $((top+y)) $left art_$y $view_w || return
  done
  if ((show_info)); then
    printf -v label ' ALPINE VIGIL | %s | %dx%d | detail 1/%d | I info · D detail · Q quit' $color_mode $view_w $((view_h*2)) $coarse
    zdraw spansclip stdscr $((screen_rows-1)) 0 $screen_cols '' "$label" || return
  fi
  zdraw stage stdscr && zdraw present || return
  ((present_ms=(SECONDS-began)*1000))
  return 0
}

view-budget-color() {
  local original=$1 hex best_hex
  if [[ -n ${view_nearest_color[$original]-} ]]; then REPLY=$view_nearest_color[$original]; return 0; fi
  local -i r=$((16#${original[2,3]})) g=$((16#${original[4,5]})) b=$((16#${original[6,7]})) distance best=1000000
  for hex in $painting_fallback_rgb; do
    ((distance=(r-16#${hex[1,2]})**2*2+(g-16#${hex[3,4]})**2*3+(b-16#${hex[5,6]})**2*2))
    if ((distance<best)); then best=$distance best_hex=$hex; fi
  done
  REPLY=\#$best_hex
  view_nearest_color[$original]=$REPLY
  return 0
}

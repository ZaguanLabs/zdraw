#!/usr/bin/env zsh
# Experimental two-pane selection. Run with the matching built shell, --mouse.
emulate -R zsh
setopt no_aliases
typeset selection_demo_root=${${(%):-%x}:A:h:h}
source "$selection_demo_root/lib/zdraw-text-selection.zsh" || return 1

function selection-demo {
  emulate -L zsh
  local -i mouse=0 mono=0 enabled=1 modal=0 dirty=1 paint=1 revision=1 first=0 frame=0
  local option policy=unicode-17.0.0-egc-wcwidth-sum-attach-zero
  for option in "$@"; do
    case $option in
      --mouse) mouse=1 ;; --mono) mono=1 ;; --native) policy=native ;;
      *) print -ru2 -- 'Usage: text-selection.zsh [--mouse] [--mono] [--native]'; return 1 ;;
    esac
  done
  module_path=("$selection_demo_root/.build/modules" "${module_path[@]}")
  zmodload zdraw || return
  local -A zdraw_text_selection zdraw_selection_event info cache
  local -a dimensions records allrows reply flags policy_arg draw_policy
  local REPLY copied='' note='' line remaining piece style key signature
  local source=$'This pane owns its selection. Drag into the sidebar or footer: neither belongs to the copied text. Soft wraps preserve this paragraph.\n\n  for item in alpha beta; do\n    print -r -- "$item"\n  done\n\nWide text: 界 and combining é.\nGrapheme: 👩‍💻 (native summed cell widths).'
  if [[ $policy != native ]] && ! zdraw textpolicy info "$policy" 2>/dev/null; then policy=native; fi
  [[ $policy == native ]] || { policy_arg=("$policy"); draw_policy=("policy=$policy"); }
  if ! zdraw textinfo info 界 8 "${policy_arg[@]}" 2>/dev/null; then
    source=$'This pane owns its selection. Drag into the sidebar or footer: neither belongs to the copied text. Soft wraps preserve this paragraph.\n\n  for item in alpha beta; do\n    print -r -- "$item"\n  done\n\nASCII fallback.'
  fi
  local -i rows cols width height sidebar r offset n total i visible=0 neighbour=0
  zdraw init || return
  {
    zdraw timeout stdscr 100 || return
    if (( mouse )); then
      zdraw mouse delay 0 motion || { mouse=0; note='Mouse unavailable; f shows all text.'; }
    else
      note='Run with --mouse to select; f shows all text.'
    fi
    while true; do
      if (( dirty )); then
        paint=1
        zdraw position stdscr dimensions || return
        rows=$dimensions[5] cols=$dimensions[6]
        # Terminals may reflow their old cells on resize; invalidate curses'
        # physical-screen assumptions when rebuilding the complete layout.
        zdraw clear stdscr redraw || return
        cache=() visible=0
        if (( rows >= 12 && cols >= 28 )); then
          sidebar=$(( cols >= 60 ? 17 : 9 )) width=$((cols-sidebar-5)) height=$((rows-9))
          records=() allrows=() offset=0 total=0 remaining=$source
          while true; do
            line=${remaining%%$'\n'*}
            [[ $line == '  '* ]] && style=bold || style=''
            (( ! mono )) && [[ $line == '  '* ]] && style=bold,cyan/default
            while true; do
              zdraw textinfo info "$line" "$width" "${policy_arg[@]}" || return
              piece=$info[text]
              [[ -n $piece || -z $line ]] || return 1
              allrows+=(0 "$offset" "$style" "$piece")
              _zdraw_ts_bytes "$piece"; (( offset += REPLY, total++ ))
              [[ $info[truncated] == 1 ]] || break
              line=$info[remainder]
            done
            [[ $remaining == *$'\n'* ]] || break
            remaining=${remaining#*$'\n'}
            (( offset++ ))
          done
          (( first >= total )) && first=$((total-1))
          for (( r=0; r<height && r+first<total; r++ )); do
            i=$(((r+first)*4+1))
            records+=("$r" "${(@)allrows[i,i+3]}")
          done
          zdraw-text-selection-init "$source" "$revision" 2 2 "$height" "$width" "$policy" "${records[@]}" || return
          visible=1
          zdraw fill stdscr 1 1 1 $((width+2)) '' '-' || return
          zdraw fill stdscr $((height+2)) 1 1 $((width+2)) '' '-' || return
          zdraw fill stdscr 2 1 "$height" 1 '' '|' || return
          zdraw fill stdscr 2 $((width+2)) "$height" 1 '' '|' || return
          zdraw spansclip stdscr 0 1 $((cols-2)) bold 'Confined text selection' || return
          zdraw spansclip stdscr 1 $((width+4)) "$sidebar" underline 'SIDEBAR' || return
          zdraw spansclip stdscr 3 $((width+4)) "$sidebar" '' 'NOT TEXT' || return
          zdraw spansclip stdscr 5 $((width+4)) "$sidebar" '' "Clicks:$neighbour" || return
          zdraw spansclip stdscr $((height+3)) 1 $((cols-2)) underline 'Explicit copy (c), escaped so real newlines are visible:' || return
        else
          [[ -n ${zdraw_text_selection[format]-} ]] && zdraw-text-selection-invalidate
          zdraw spansclip stdscr 0 0 "$cols" '' 'Need 28x12; q quits.' || return
        fi
        dirty=0
      fi
      if (( visible && paint )); then
        for (( r=0; r<height; r++ )); do
          zdraw-text-selection-spans "$r" || return
          signature=${(j:\0:)reply}
          if [[ ${cache[$r]-unset} != "$signature" ]]; then
            if (( ${#reply} )); then
              zdraw spans stdscr $((r+2)) 2 "${draw_policy[@]}" "${reply[@]}" || return
            fi
            cache[$r]=$signature
          fi
        done
        zdraw fill stdscr $((height+4)) 1 2 $((cols-2)) '' ' ' || return
        remaining=${(qqqq)copied}
        for (( r=height+4; r<height+6; r++ )); do
          zdraw textinfo info "$remaining" $((cols-2)) || return
          zdraw spansclip stdscr "$r" 1 $((cols-2)) '' "$info[text]" || return
          remaining=$info[remainder]
        done
        zdraw fill stdscr $((rows-2)) 0 1 "$cols" '' ' ' || return
        if (( cols >= 80 )); then
          line='c copy | Esc clear | j/k scroll | a append | m modal | d mouse | f all | q quit'
        else
          line='c copy | Esc clear | q quit | f all'
        fi
        zdraw spansclip stdscr $((rows-2)) 0 "$cols" '' "$line" || return
        zdraw fill stdscr $((rows-1)) 0 1 "$cols" '' ' ' || return
        zdraw spansclip stdscr $((rows-1)) 0 $((cols-1)) '' "${note:-Drag with left button.} mouse=$((mouse && enabled && !modal)) modal=$modal" || return
      fi
      if (( paint )); then zdraw refresh || return; paint=0; fi
      (( frame++ ))
      # Optional test observer; does not participate in ordinary input handling.
      (( ${+functions[selection-demo-observe]} )) && selection-demo-observe
      flags=()
      (( mouse && enabled && !modal )) && flags+=(mouse)
      (( ${zdraw_features[(Ie)norefresh_events]} )) && flags+=(norefresh)
      if ! zdraw event stdscr zdraw_selection_event "${flags[@]}"; then continue; fi
      if [[ $zdraw_selection_event[type] == resize ]]; then
        [[ -n ${zdraw_text_selection[format]-} ]] && zdraw-text-selection-invalidate
        zdraw resize "$zdraw_selection_event[rows]" "$zdraw_selection_event[columns]" nosave || return
        (( revision++ )); dirty=1; continue
      fi
      if (( visible && enabled && !modal )); then
        zdraw-text-selection-event "$revision" || return
        (( zdraw_text_selection[changed] )) && paint=1
        (( zdraw_text_selection[consumed] )) && continue
      fi
      if [[ $zdraw_selection_event[type] == mouse ]]; then
        if [[ " $zdraw_selection_event[buttons] " == *' PRESSED1 '* ]] && (( zdraw_selection_event[x] >= width+4 )); then
          (( neighbour++ )); dirty=1
        fi
      elif [[ $zdraw_selection_event[type] == character ]]; then
        key=$zdraw_selection_event[text]
        case $key in
          q) break ;;
          c) if (( visible )); then zdraw-text-selection-get || return; copied=$REPLY; paint=1; fi ;;
          f) copied=$source; paint=1 ;;
          j|k)
            if [[ $key == j ]]; then (( first++ )); else (( first=first>0 ? first-1 : 0 )); fi
            (( revision++ )); dirty=1 ;;
          a) source+=$'\nAppended output.'; (( revision++ )); dirty=1 ;;
          m|d)
            paint=1
            [[ -n ${zdraw_text_selection[format]-} ]] && zdraw-text-selection-clear
            if [[ $key == m ]]; then (( modal=1-modal )); else (( enabled=1-enabled )); fi
            if (( mouse )); then
              if (( enabled && !modal )); then zdraw mouse motion; else zdraw mouse -motion; fi
            fi ;;
        esac
      fi
    done
  } always {
    [[ -n ${zdraw_text_selection[format]-} ]] && zdraw-text-selection-invalidate
    (( mouse )) && zdraw mouse -motion
    zdraw end
  }
}
selection-demo "$@"

#!/usr/bin/env zsh
# Synchronize an unmodified interactive example at presentation boundaries.
emulate -R zsh
typeset report_fd=$1 control_fd=$2 capture_dir=${3:-}
typeset example=${4:-gallery}
[[ $example == (gallery|list-detail|table-inspector|task-monitor|color-studio) ]] || exit 1
typeset -i test_ui_frame_number=0
function zdraw {
  builtin zdraw "$@" || return
  case $1 in
    refresh|present)
      (( test_ui_frame_number++ ))
      local -A cells
      if [[ $example == task-monitor ]] && (( tab == 1 && rows >= 9 && columns >= 24 )); then
        local -i test_ui_average=0 test_ui_value test_ui_width test_ui_origin test_ui_c
        local test_ui_expected test_ui_actual=''
        for test_ui_value in "${progress[@]}"; do (( test_ui_average += test_ui_value )); done
        (( test_ui_average /= ${#tasks} ))
        test_ui_expected="     $test_ui_average%"
        test_ui_expected=${test_ui_expected[-5,-1]}
        test_ui_width=$(( columns < 112 ? columns : 112 ))
        test_ui_origin=$(( (columns-test_ui_width)/2 ))
        builtin zdraw snapshot stdscr cells || return
        for (( test_ui_c=test_ui_origin+test_ui_width-7; test_ui_c<test_ui_origin+test_ui_width-2; test_ui_c++ )); do
          test_ui_actual+="$cells[5,$test_ui_c,text]"
        done
        [[ $test_ui_actual == "$test_ui_expected" ]] || { print -ru2 -- 'FAIL: rendered progress label'; return 1; }
      fi
      if [[ $example == task-monitor ]] && (( tab == 3 && rows >= 9 && columns >= 24 )); then
        local -i test_chart_width=$(( columns < 112 ? columns : 112 )) test_chart_x test_chart_c
        local test_chart_actual=''
        test_chart_x=$(((columns-test_chart_width)/2+2))
        builtin zdraw snapshot stdscr cells || return
        for (( test_chart_c=test_chart_x; test_chart_c<test_chart_x+8; test_chart_c++ )); do
          test_chart_actual+=$cells[4,$test_chart_c,text]
        done
        [[ $test_chart_actual == 'Overall:' ]] || { print -ru2 -- 'FAIL: history title'; return 1; }
        (( ${#chart_history} <= 96 )) || { print -ru2 -- 'FAIL: unbounded chart history'; return 1; }
        if [[ $chart_palette == ascii && $chart_history[-1] == 100 ]] && (( rows >= 10 )); then
          test_chart_c=$((test_chart_x + (${#chart_history} < test_chart_width-4 ? ${#chart_history} : test_chart_width-4) - 1))
          [[ $cells[5,$test_chart_c,text] == '@' ]] || { print -ru2 -- 'FAIL: last history sample'; return 1; }
        fi
      fi
      if [[ $example == (list-detail|table-inspector) && $layout_mode != tiny ]]; then
        local screen='' row_text expected_details
        local -i test_ui_selected
        if [[ $example == list-detail ]]; then
          test_ui_selected=$zdraw_ui_list[selected]
        else
          test_ui_selected=$zdraw_ui_table[selected]
        fi
        if (( test_ui_selected )); then
          expected_details="$stages[$test_ui_selected] / $owners[$test_ui_selected]"
        else
          expected_details='No project selected'
        fi
        local -i r c
        builtin zdraw snapshot stdscr cells || return
        for (( r=0; r<rows; r++ )); do
          row_text=''
          for (( c=0; c<columns; c++ )); do row_text+="$cells[$r,$c,text]"; done
          screen+="$row_text"$'\n'
        done
        if [[ $layout_mode == split || $view == detail ]]; then
          [[ $screen == *"$expected_details"* ]] || { print -ru2 -- 'FAIL: selected details missing'; return 1; }
        else
          [[ $screen != *"$expected_details"* ]] || { print -ru2 -- 'FAIL: stale details'; return 1; }
        fi
      fi
      if [[ -n $capture_dir ]]; then
        local -i r c
        builtin zdraw snapshot stdscr cells || return
        {
          print -r -- "$rows $columns"
          for (( r=0; r<rows; r++ )); do
            for (( c=0; c<columns; c++ )); do
              print -r -- "$r\t$c\t$cells[$r,$c,color]\t$cells[$r,$c,attributes]\t$cells[$r,$c,text]"
            done
          done
        } > "$capture_dir/$example-$test_ui_frame_number.cells"
      fi
      if [[ $example == color-studio ]]; then
        print -r -u "$report_fd" -- "colors $rows $columns $theme_name $profile"
      elif [[ $example == task-monitor ]]; then
        print -r -u "$report_fd" -- "monitor $rows $columns $tab $paused $tick $completed $zdraw_ui_table[selected] $theme_name $chart_palette $profile ${#chart_history} $chart_history[-1]"
      elif [[ $example == gallery ]]; then
        print -r -u "$report_fd" -- "frame $rows $columns $theme_name $profile $border_index $compact $empty $narrow $list_focus ${zdraw_ui_list[selected]:-0}"
      elif [[ $example == list-detail ]]; then
        print -r -u "$report_fd" -- "recipe $rows $columns $layout_mode $view $theme_name ${zdraw_ui_list[selected]:-0}"
      else
        print -r -u "$report_fd" -- "inspector $rows $columns $layout_mode $view $theme_name ${zdraw_ui_table[selected]:-0} $empty"
      fi
      local acknowledgement
      read -r -u "$control_fd" acknowledgement ;;
    end) print -r -u "$report_fd" -- done ;;
  esac
  return 0
}
print -r -u "$report_fd" -- baseline
read -r -u "$control_fd" acknowledgement
typeset test_recipe_path=${0:A:h:h}/examples/$example.zsh
() { source "$test_recipe_path"; } "${@:5}"

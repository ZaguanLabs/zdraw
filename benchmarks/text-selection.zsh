#!/usr/bin/env zsh
# Headless retained drag and span measurements with the matching built shell.
emulate -R zsh
setopt nounset
typeset root=${0:A:h:h}
module_path=("$root/.build/modules")
zmodload zdraw || exit 1
source "$root/lib/zdraw-text-selection.zsh" || exit 1
typeset -A zdraw_text_selection zdraw_selection_event
typeset -a records reply
typeset source line='A representative row of prose with retained styles and indentation.   ' REPLY
typeset -i lines n i r sample iterations=1000 bytes
typeset -F 9 SECONDS started compiled events spans
print -r -- 'lines bytes compile_ms sample event_us spans_24_rows_us'
for lines in 100 10000; do
  source='' records=()
  for (( n=0; n<lines; n++ )); do source+="$line"$'\n'; done
  _zdraw_ts_bytes "$source"; bytes=$REPLY
  for (( r=0; r<24; r++ )); do records+=("$r" 0 "$((r*(${#line}+1)))" bold "$line"); done
  started=$SECONDS
  zdraw-text-selection-init "$source" r 0 0 24 80 native "${records[@]}" || exit 1
  compiled=$((1000*(SECONDS-started)))
  zdraw_selection_event=(type mouse x 0 y 0 buttons PRESSED1 modifiers '')
  zdraw-text-selection-event r || exit 1
  zdraw_selection_event[buttons]=''
  for (( sample=1; sample<=5; sample++ )); do
    started=$SECONDS
    for (( i=0; i<iterations; i++ )); do
      zdraw_selection_event[x]=$((i%90)) zdraw_selection_event[y]=$((i%28))
      zdraw-text-selection-event r || exit 1
    done
    events=$((1000000*(SECONDS-started)/iterations))
    started=$SECONDS
    for (( i=0; i<20; i++ )); do
      for (( r=0; r<24; r++ )); do zdraw-text-selection-spans "$r" || exit 1; done
    done
    spans=$((1000000*(SECONDS-started)/20))
    printf '%d %d %.3f %d %.3f %.3f\n' "$lines" "$bytes" "$compiled" "$sample" "$events" "$spans"
  done
done

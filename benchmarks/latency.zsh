#!/usr/bin/env zsh
# Reports completed presentations, not emulator paint or physical display time.
emulate -R zsh
setopt errexit nounset
module_path=("$1")
typeset report_fd=$2 text pattern
typeset -i frames=$3 frame row rows=24 columns=80
typeset -A event
zmodload zdraw
zdraw init
{
  zdraw timeout stdscr 1000
  print -r -u "$report_fd" -- ready
  for (( frame=0; frame<frames; frame++ )); do
    while true; do
      zdraw event stdscr event norefresh
      if [[ $event[type] == resize ]]; then
        rows=$event[rows] columns=$event[columns]
        zdraw resize "$rows" "$columns" nosave
      else
        [[ $event[type] == character ]] || exit 2
        break
      fi
    done
    pattern=0123456789abcdefghijklmnopqrstuvwxyz
    (( frame % 2 == 0 )) || pattern=ZYXWVUTSRQPONMLKJIHGFEDCBA9876543210
    text=${(pl:$((columns-4))::$pattern:):-}
    for (( row=0; row<rows; row++ )); do
      zdraw spans stdscr "$row" 0 '' "${(l:4::0:)row}$text"
    done
    # Include repeated explicit terminal ownership handoffs.
    if (( frame > 0 && frame % 40 == 0 )); then
      zdraw suspend
      zdraw resume
    fi
    zdraw stage stdscr
    zdraw present
    print -r -u "$report_fd" -- "$frame $event[text]"
  done
} always {
  zdraw end
}
print -r -u "$report_fd" -- done

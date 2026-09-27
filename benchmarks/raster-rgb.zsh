#!/usr/bin/env zsh
# Finished-frame presentation only. Driver drains a PTY and receives timings separately.
emulate -R zsh
setopt nounset
module_path=("$1")
typeset input=$2 report_fd=$4 backend=$5
fail() { print -ru2 -- "FAIL: $*"; exit 1; }
check() { "$@" || fail "$*"; }
zmodload zdraw && zmodload zsh/mathfunc || exit 1
typeset -F 9 SECONDS began setup_time pack_time present_time
began=$SECONDS
typeset -a rgb pixels zeros ids fields surfaces
typeset -A plan stats
typeset -i art_w=750 art_h=396 painting_colors=1 t y x
if [[ $backend == reference ]]; then
  {
    read -rA rgb
    read -rA ids
  } < "$input/frame"
  zeros=(${ids/*/0})
  pixels=(${ids:^zeros})
  source "${0:A:h}/alpine-view-reference.zsh"
  color_mode=rgb
else
  for ((t=0;t<9;t++)); do
    surfaces+=(tile$t)
    check zdraw raster create-rgb tile$t 250 132 '#000008' '.'
    while read -rA fields; do
      check zdraw raster triangles-rgb tile$t "${fields[@]}"
    done < "$input/tile$t"
  done
fi
((setup_time=SECONDS-began))
check zdraw init
{
  check zdraw truecolor on
  began=$SECONDS
  if [[ $backend == reference ]]; then
    check view-prepare
    ((pack_time=SECONDS-began))
    began=$SECONDS
    check view-present
  else
    # Reserve the same background pair before the aggregate exact check.
    check zdraw fill stdscr 0 0 198 750 black/black ' '
    check zdraw raster plan tile0 plan "${(@)surfaces[2,-1]}"
    (( plan[fits] )) || fail budget
    for ((t=0;t<9;t++)); do
      ((y=t/3*66,x=t%3*250))
      check zdraw raster blit tile$t stdscr $y $x half
    done
    ((pack_time=SECONDS-began))
    began=$SECONDS
    check zdraw stage stdscr
    check zdraw present
  fi
  ((present_time=SECONDS-began))
  check zdraw colorinfo stats
  (( stats[pairs_used] == 15718 )) || fail "pairs $stats[pairs_used]"
} always {
  zdraw end
}
print -u $report_fd -r -- "1 $setup_time $pack_time $present_time $((pack_time+present_time))"

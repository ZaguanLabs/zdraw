#!/usr/bin/env zsh
# Replay captured render-rect inputs; preparation deliberately includes calls
# and array appends as in Cinder Relay. Geometry/projection is already captured.
emulate -R zsh
setopt errexit nounset
module_path=("$1")
typeset input=$2 report_fd=$4 backend=$5 output_mode=$6 line
typeset -i repetitions=$3 width height iteration count=0 i start stride
typeset -a frames rects batch palette inks
typeset x0 y0 x1 y1 qt qb material glyphs
typeset -F 9 SECONDS started elapsed split_time=0 construct_time=0 clear_time=0
typeset -F 9 raster_time=0 pack_time=0 present_time=0 total_started total_time
{
  read -r width height
  IFS= read -r line
  inks=(${=line})
  IFS= read -r glyphs
  for ((i=1;i<=${#inks};i++)); do palette+=("$inks[i]" "$glyphs[i]"); done
  while IFS= read -r line; do frames+=("$line"); done
} < "$input"
zmodload zdraw
zdraw raster create scene $width $height "${palette[@]}"
if [[ $backend == triangles ]]; then
  stride=40960
  benchmark-rect() {
    (($4>$2 && $3>$1)) || return 0
    batch+=($1 $2 $5 $3 $2 $5 $3 $4 ${7:-$5} $6
            $1 $2 $5 $3 $4 ${7:-$5} $1 $4 ${7:-$5} $6)
  }
else
  stride=14336
  benchmark-rect() {
    (($4>$2 && $3>$1)) || return 0
    batch+=($1 $2 $3 $4 $5 ${7:-$5} $6)
  }
fi
if [[ $output_mode != headless ]]; then
  zdraw init
  zdraw addwin scene $(((height+1)/2)) $width 0 0
fi
{
  total_started=$SECONDS
  for ((iteration=0;iteration<=repetitions;iteration++)); do
    if ((iteration==1)); then total_started=$SECONDS; fi
    for line in "${frames[@]}"; do
      started=$SECONDS
      rects=(${=line})
      elapsed=$((SECONDS-started))
      if ((iteration)); then ((split_time+=elapsed)); fi
      started=$SECONDS
      batch=()
      for x0 y0 x1 y1 qt qb material in "${rects[@]}"; do
        benchmark-rect $x0 $y0 $x1 $y1 $qt $material $qb
      done
      elapsed=$((SECONDS-started))
      if ((iteration)); then ((construct_time+=elapsed)); fi
      started=$SECONDS
      zdraw raster clear scene 0 1
      elapsed=$((SECONDS-started))
      if ((iteration)); then ((clear_time+=elapsed)); fi
      started=$SECONDS
      for ((start=1;start<=${#batch};start+=stride)); do
        zdraw raster $backend scene "${batch[@][$start,$((start+stride-1))]}"
      done
      elapsed=$((SECONDS-started))
      if ((iteration)); then ((raster_time+=elapsed)); fi
      if [[ $output_mode != headless ]]; then
        started=$SECONDS
        zdraw raster blit scene scene 0 0 $output_mode
        elapsed=$((SECONDS-started))
        if ((iteration)); then ((pack_time+=elapsed)); fi
        started=$SECONDS
        zdraw stage scene
        zdraw present
        elapsed=$((SECONDS-started))
        if ((iteration)); then ((present_time+=elapsed)); fi
      fi
      if ((iteration)); then ((count++)) || true; fi
    done
  done
  total_time=$((SECONDS-total_started))
} always {
  zdraw end
}
printf '%d %.9f %.9f %.9f %.9f %.9f %.9f %.9f\n' $count $split_time $construct_time \
  $clear_time $raster_time $pack_time $present_time $total_time >&$report_fd

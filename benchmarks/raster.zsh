#!/usr/bin/env zsh
# Driven by raster.py, with a separate report descriptor and drained PTY.
emulate -R zsh
setopt errexit nounset
module_path=("$1")
typeset input=$2 report_fd=$4 backend=$5 output_mode=$6 line
typeset -i repetitions=$3 width height iteration frame nframes count=0 i y x upper lower index
typeset -i wireframe=0 triangles_drawn=0 fragments=0
typeset -a frames triangle pixels depth spans
typeset -a ink=(0 233 235 235 238 242 248 52 94 136 222 23 30 44 159)
typeset -a density=(' ' ' ' '.' '.' ':' '+' '#' '.' ':' '*' '@' ':' '+' '*' '@')
typeset -a palette
typeset -F 9 SECONDS started elapsed split_time=0 clear_time=0 raster_time=0 pack_time=0 present_time=0
typeset -F 9 total_started total_time
typeset last run style glyph
zmodload zdraw
if [[ $backend == reference ]]; then
  zmodload zsh/mathfunc
  source "${0:A:h}/raster-reference.zsh"
fi
{
  read -r width height
  while IFS= read -r line; do frames+=("$line"); done
} < "$input"
nframes=${#frames}
for ((i=1;i<=${#ink};i++)); do palette+=($ink[i] "$density[i]"); done
zdraw raster create scene $width $height "${palette[@]}"
if [[ $output_mode != headless ]]; then
  zdraw init
  zdraw addwin scene $(((height+1)/2)) $width 0 0
fi
{
  total_started=$SECONDS
  # First sweep warms caches; subsequent sweeps are measured.
  for ((iteration=0;iteration<=repetitions;iteration++)); do
    if ((iteration==1)); then total_started=$SECONDS; fi
    for ((frame=1;frame<=nframes;frame++)); do
      started=$SECONDS
      triangle=(${=frames[frame]})
      elapsed=$((SECONDS-started))
      if ((iteration)); then ((split_time+=elapsed)); fi
      started=$SECONDS
      if [[ $backend == reference ]]; then
        zeros=${(pl:$((width*height*2))::0 :):-}
        sky=${(pl:$((width*(height/2)*2))::1 :):-}
        ground=${(pl:$((width*(height-height/2)*2))::2 :):-}
        depth=(${=zeros}) pixels=(${=sky} ${=ground})
      else
        zdraw raster clear scene 1 2
      fi
      elapsed=$((SECONDS-started))
      if ((iteration)); then ((clear_time+=elapsed)); fi
      started=$SECONDS
      if [[ $backend == reference ]]; then
        for ((i=1;i<=${#triangle};i+=10)); do
          raster-reference "${triangle[@]:$((i-1)):10}" || exit 1
        done
      else
        zdraw raster triangles scene "${triangle[@]}"
      fi
      elapsed=$((SECONDS-started))
      if ((iteration)); then ((raster_time+=elapsed)); fi
      if [[ $output_mode != headless ]]; then
        started=$SECONDS
        if [[ $backend == reference ]]; then
          for ((y=0;y<height/2;y++)); do
            spans=() last='' run=''
            for ((x=0;x<width;x++)); do
              ((index=2*y*width+x+1, upper=pixels[index]+1, lower=pixels[index+width]+1))
              if [[ $output_mode == mono ]]; then
                style='' glyph=$density[upper]
              elif [[ $output_mode == ascii ]]; then
                style="$ink[upper]/$ink[1]" glyph=$density[upper]
              else
                style="$ink[upper]/$ink[lower]" glyph='▀'
                if ((upper==lower)); then glyph=' '; fi
              fi
              if [[ $style == $last ]]; then run+=$glyph
              else
                if [[ -n $run ]]; then spans+=("$last" "$run"); fi
                last=$style run=$glyph
              fi
            done
            if [[ -n $run ]]; then spans+=("$last" "$run"); fi
            zdraw spans scene $y 0 "${spans[@]}"
          done
        else
          zdraw raster blit scene scene 0 0 $output_mode
        fi
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
printf '%d %.9f %.9f %.9f %.9f %.9f %.9f\n' $count $split_time $clear_time $raster_time $pack_time $present_time $total_time >&$report_fd

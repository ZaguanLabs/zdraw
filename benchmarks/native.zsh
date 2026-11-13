#!/usr/bin/env zsh
# Driven by native.py in a drained PTY using a matching shell and module.
emulate -R zsh
setopt errexit nounset
module_path=("$1")
typeset workload=$2 report_fd=$4 text key
typeset -i iterations=$3 i reuse=0 draw_index generation=0
typeset -A result
typeset -F 9 SECONDS elapsed
zmodload zdraw
zdraw init
{
  zdraw addwin sample 20 70 0 0
  text=${(pl:64::abcdefgh:):-}
  case $workload in
    *distinct*) text='0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!?' ;;
    *unicode*) text='界é界é界é界é界é界é界é界é' ;;
    *long*) text=${(pl:8192::abcdefgh:):-} ;;
  esac
  zdraw attr sample red/black blue/black default/default
  zdraw prepare ready bold,red/black abcdefghijklmnop
  [[ $workload != reuse-* ]] || reuse=${workload##*-}
  case $workload in
    lookup)
      for (( i=0; i<256; i++ )); do zdraw addwin "w$i" 1 1 0 0; done ;;
    snapshot)
      for (( i=0; i<20; i++ )); do
        zdraw spans sample "$i" 0 "$((i+1))/black" "$text"
      done ;;
  esac
  function work {
    case $workload in
      reuse-*)
        # Charge preparation and disposal to every group. The row changes
        # between groups; both backends draw the same text the same number of times.
        generation=$((1-generation))
        text="${generation}${(pl:63::abcdefgh:):-}"
        if [[ $workload == reuse-prepared-* ]]; then
          zdraw prepare temporary bold,red/black "$text"
          for (( draw_index=0; draw_index<reuse; draw_index++ )); do
            zdraw draw sample 0 0 temporary
          done
          zdraw unprepare temporary
        else
          for (( draw_index=0; draw_index<reuse; draw_index++ )); do
            zdraw spans sample 0 0 bold,red/black "$text"
          done
        fi ;;
      textinfo*) zdraw textinfo result "$text" 32 ;;
      textpos*) zdraw textpos result "$text" column 12 ;;
      grapheme*) zdraw textinfo result "$text" 32 grapheme ;;
      safe-query*) zdraw textinfo result "$text" 32 unicode-17.0.0-egc-wcwidth-sum-attach-zero ;;
      wrap*) zdraw textwrap result "$text" 32 ;;
      spans*) zdraw spans sample 0 0 bold,red/black "$text" ;;
      clip*) zdraw spansclip sample 0 0 8 bold,red/black "$text" ;;
      safe-spans*) zdraw spans sample 0 0 policy=unicode-17.0.0-egc-wcwidth-sum-attach-zero bold,red/black "$text" ;;
      prepare) zdraw prepare temporary bold,red/black "$text"; zdraw unprepare temporary ;;
      prepared) zdraw draw sample 0 0 ready ;;
      fill) zdraw fill sample 0 0 20 64 bold,red/black X ;;
      snapshot) zdraw snapshot sample result ;;
      lookup) zdraw move w0 0 0 ;;
      *) return 1 ;;
    esac
  }
  for (( i=0; i<25; i++ )); do work; done
  SECONDS=0
  for (( i=0; i<iterations; i++ )); do work; done
  elapsed=$SECONDS
  # Export actual query results or final cells outside the measured interval.
  (( ${#result} )) || zdraw snapshot sample result
} always {
  zdraw end
}
print -r -u "$report_fd" -- "$elapsed"
for key in "${(@ok)result}"; do
  print -rn -u "$report_fd" -- "$key"$'\0'"$result[$key]"$'\0'
done

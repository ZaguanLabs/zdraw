#!/usr/bin/env zsh
emulate -R zsh
setopt nounset
module_path=("$1")
typeset mode=$2
zmodload zdraw || exit 1
fail() { print -ru2 -- "FAIL: $*"; exit 1; }
check() { "$@" || fail "$*"; }
typeset -A plan info before after
typeset -a pairs
typeset -i i result
typeset value
check zdraw init
{
  check zdraw colorplan plan attr
  [[ $plan[fits] == 1 && $plan[requested] == 0 && $plan[max_pair] == 0 ]] || fail empty
  if [[ $mode == silent || $mode == silent_control ]]; then
    if [[ $mode == silent ]]; then
      check zdraw colorplan plan spans red/black blue/black red/black
    fi
  elif [[ $mode == monochrome || $mode == failed_start || $mode == defaults_failed || $mode == no_defaults ]]; then
    zdraw colorplan plan attr red/black 2>/dev/null
    result=$?
    if [[ $mode == defaults_failed || $mode == no_defaults ]]; then
      (( result == 0 )) || fail 'ordinary colors with failed defaults'
      zdraw colorplan plan attr default/default 2>/dev/null
      result=$?
      if [[ $mode == no_defaults ]]; then
        (( result == 1 )) || fail 'uncompiled default spelling'
      else
        (( result == 2 )) || fail 'failed default support'
      fi
      check zdraw attr stdscr red/black
      check zdraw colorplan plan attr default/default
      [[ $plan[pairs_reused] == 1 && $plan[max_pair] == 0 ]] || fail 'seeded default cache'
    else
      (( result == 2 )) || fail 'unavailable colors'
    fi
  elif [[ $mode == no_spans ]]; then
    zdraw colorplan plan spans
    (( $? == 2 )) || fail 'unavailable writer'
  elif [[ $mode == rgb ]]; then
    zdraw colorplan plan spans '#112233/#445566' 2>/dev/null && fail 'implicit RGB opt-in'
    check zdraw truecolor on
    check zdraw colorplan plan spans '#112233/#445566' '#112233/#445566' '#abcdef/black' '#ABCDEF/black'
    [[ $plan[unique] == 3 && $plan[pairs_needed] == 3 && $plan[fits] == 1 ]] || fail 'RGB spellings'
    check zdraw prepare rgb '#112233/#445566' X '#abcdef/black' Y '#ABCDEF/black' Z
    check zdraw colorinfo info
    [[ $info[pairs_used] == $plan[pairs_needed] ]] || fail 'RGB prediction'
    check zdraw colorplan plan spans '#112233/#445566'
    [[ $plan[pairs_needed] == 0 && $plan[pairs_reused] == 1 ]] || fail 'RGB reuse'
    check zdraw truecolor off
    zdraw colorplan plan spans '#112233/#445566' 2>/dev/null && fail 'RGB cached opt-out'
    check zdraw truecolor on
    zdraw colorplan plan spans '#000000/black' 2>/dev/null && fail 'reserved RGB'
  else
    # Passive queries must not consume the inherited first-allocation phase.
    plan=(keep value)
    zdraw colorplan plan spans red/black invalid/nope 2>/dev/null && fail 'invalid late pair'
    [[ $plan[keep] == value && ${#plan} == 1 ]] || fail 'late failure assignment'
    check zdraw colorplan plan attr default/default default/default
    [[ $plan[pairs_needed] == 1 && $plan[pairs_reused] == 0 ]] || fail 'default first'
    check zdraw colorplan plan attr red/black default/default
    [[ $plan[pairs_needed] == 1 && $plan[pairs_reused] == 1 ]] || fail 'default second'
    check zdraw colorinfo info
    [[ $info[pairs_used] == 0 ]] || fail 'query allocated'
    if [[ $mode == allocation_failure ]]; then
      [[ $plan[fits] == 1 ]] || fail 'capacity with failing library'
      zdraw attr stdscr red/black 2>/dev/null && fail 'injected allocation succeeded'
      check zdraw colorinfo info
      [[ $info[pairs_used] == 0 ]] || fail 'failed library allocated'
    else
      check zdraw attr stdscr default/default
      check zdraw colorinfo info
      [[ $info[pairs_used] == 1 ]] || fail 'first-use phase changed'
      check zdraw colorplan plan spans red/black 1/0 red/black
      [[ $plan[requested] == 3 && $plan[unique] == 2 && $plan[pairs_needed] == 2 ]] || fail 'deduplication'
      check zdraw prepare first red/black X 1/0 Y
      check zdraw colorinfo info
      [[ $info[pairs_used] == 3 ]] || fail 'prediction differs from preparation'
      check zdraw colorplan plan spans red/black 1/0 default/default
      [[ $plan[pairs_needed] == 0 && $plan[pairs_reused] == 3 && $plan[fits] == 1 ]] || fail reuse
      if [[ $mode == small ]]; then
        check zdraw colorplan plan spans blue/black red/black
        [[ $plan[pairs_needed] == 1 && $plan[pairs_free] == 0 && $plan[fits] == 0 ]] || fail exhaustion
        zdraw prepare overflow blue/black X 2>/dev/null && fail 'exhaustion prediction'
        check zdraw draw stdscr 0 0 first
      else
        # Synthetic fixture at the harness's reported pair count.
        for (( i=0; i<15718; i++ )); do pairs+=("$((i/256))/$((i%256))"); done
        check zdraw colorplan plan spans "${pairs[@]}"
        [[ $plan[unique] == 15718 && $plan[pairs_needed] == 15717 ]] || fail 'large frame'
        check zdraw colorinfo info
        [[ $info[pairs_used] == 3 ]] || fail 'large plan allocated'
        if [[ $mode == narrow ]]; then
          [[ $plan[fits] == 0 ]] || fail 'narrow frame budget'
          for (( i=0; i<260; i++ )); do check zdraw attr stdscr "$((i/256))/$((i%256))"; done
          check zdraw colorplan plan attr 1/3
          [[ $plan[pairs_needed] == 0 && $plan[fits] == 1 ]] || fail 'high cached attr'
          check zdraw colorplan plan spans 1/3
          [[ $plan[pairs_needed] == 0 && $plan[fits] == 0 ]] || fail 'high cached spans'
          check zdraw colorplan plan bg 1/3
          [[ $plan[fits] == 0 ]] || fail 'high cached background'
        else
          [[ $plan[fits] == 1 ]] || fail 'wide frame budget'
        fi
      fi
      check zdraw draw stdscr 2 2 first
      check zdraw move stdscr 3 4
      check zdraw snapshot stdscr before
      check zdraw resourceinfo info
      typeset saved_resources=${(j: :)${(okv)info}}
      check zdraw colorplan plan spans red/black
      check zdraw snapshot stdscr after
      [[ ${(j: :)${(okv)before}} == ${(j: :)${(okv)after}} ]] || fail 'changed cells/cursor'
      check zdraw resourceinfo info
      [[ ${(j: :)${(okv)info}} == $saved_resources ]] || fail 'changed resources'
      check zdraw suspend
      check zdraw colorplan plan spans red/black
      check zdraw resume
      plan=(keep value)
      for value in '' red red/ /black red/black/blue red/nope '#nope/black' 'bold,red/black'; do
        zdraw colorplan plan spans "$value" 2>/dev/null && fail "accepted $value"
        [[ $plan[keep] == value && ${#plan} == 1 ]] || fail 'modified invalid output'
      done
      typeset scalar=keep
      typeset -Ar frozen=(keep value)
      for value in scalar frozen functions 'plan[key]' 'bad name'; do
        zdraw colorplan "$value" spans red/black 2>/dev/null && fail "accepted output $value"
      done
      zdraw colorplan plan bogus red/black 2>/dev/null && fail 'invalid path'
      pairs=()
      for (( i=1; i<=65536; i++ )); do pairs+=(red/black); done
      check zdraw colorplan info spans "${pairs[@]}"
      [[ $info[unique] == 1 && $info[pairs_needed] == 0 && $info[requested] == 65536 ]] || fail 'request boundary'
      pairs+=(red/black)
      zdraw colorplan plan spans "${pairs[@]}" 2>/dev/null && fail 'request bound'
      value=''
      value=${(l:1048573::0:)value}
      check zdraw colorplan info spans "$value/0"
      [[ $info[requested] == 1 ]] || fail 'byte boundary'
      zdraw colorplan plan spans "${value}0/0" 2>/dev/null && fail 'byte bound'
      [[ $plan[keep] == value && ${#plan} == 1 ]] || fail 'bounds changed output'
      scoped() {
        local -A local_plan=(stale value)
        nested() { zdraw colorplan local_plan spans red/black; }
        check nested
        [[ $local_plan[pairs_needed] == 0 && ${+local_plan[stale]} == 0 ]] || fail 'local assignment'
        local line
        read -r line
        [[ $line == untouched ]] || fail 'input consumed'
      }
      scoped <<<'untouched'
      check zdraw colorplan fresh attr
      [[ ${(t)fresh} == association && $fresh[fits] == 1 ]] || fail 'create output'
    fi
  fi
} always {
  zdraw end
}
check zdraw init
check zdraw colorplan plan attr
[[ $plan[pairs_used] == 0 ]] || fail 'session restart'
check zdraw end
print -r -- 'COLORPLAN PASS'

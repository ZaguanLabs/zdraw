#!/usr/bin/env zsh
# Build against supplied Zsh sources without modifying them or installing files.
emulate -R zsh
setopt errexit nounset pipefail
typeset project_root=${0:A:h:h}
typeset source_root=${ZSH_BUILD_ROOT:?Set ZSH_BUILD_ROOT to an extracted Zsh release or configured in-tree build; see README.md}
source_root=${source_root:A}
typeset build_root=$project_root/.build/zsh
typeset make_command=${ZDRAW_MAKE:-make}
[[ -f $source_root/configure && -f $source_root/Src/zsh.h &&
   -f $source_root/Src/Modules/curses.mdd ]] || {
  print -u2 -r -- 'ZSH_BUILD_ROOT must contain Zsh sources with a configure script.'
  exit 1
}
[[ $source_root != $build_root && $source_root != $build_root/* &&
   $build_root != $source_root/* ]] || {
  print -u2 -r -- 'ZSH_BUILD_ROOT must be separate from the .build/zsh working copy.'
  exit 1
}
# Configured builds must use relative in-tree source paths so their copied
# makefiles cannot regenerate files in the supplied source tree.
if [[ -f $source_root/config.status ]]; then
  [[ -f $source_root/config.h && -f $source_root/Src/Makefile &&
     -f $source_root/Src/Modules/Makefile ]] &&
    awk '/^sdir[ \t]*=/ { if ($3 == ".") ok = 1 } END { exit !ok }' \
      "$source_root/Src/Modules/Makefile" || {
    print -u2 -r -- 'Use an extracted release or a configured in-tree build with relative source paths.'
    exit 1
  }
fi
mkdir -p "$project_root/.build"
[[ ! -f $build_root/.zcurses-configure-patch ]] || {
  print -u2 -r -- 'Build cache predates the zdraw rename; run make clean and retry.'
  exit 1
}
if [[ ! -d $build_root ]]; then
  # -R -P -p works with both POSIX/BSD and GNU cp.
  cp -R -P -p "$source_root" "$build_root"
  print -r -- "$source_root" > "$project_root/.build/source-root"
fi
[[ -f $project_root/.build/source-root && $(<"$project_root/.build/source-root") == $source_root ]] || {
  print -u2 -r -- 'Build cache belongs to another source tree; run make clean and retry.'
  exit 1
}
cp "$project_root"/Src/Modules/{zdraw.c,zdraw.mdd,zdraw_keys.awk,zdraw_grapheme.h,zdraw_grapheme_data.h,zdraw_raster.h} "$build_root/Src/Modules/"
cp "$project_root/Doc/Zsh/mod_zdraw.yo" "$build_root/Doc/Zsh/"
# Add optional drawing checks and register the manual in Zsh's build machinery.
# Only the disposable working copy is patched, including for configured inputs.
typeset build_patch=$project_root/patches/zdraw-build.patch
typeset build_stamp=$build_root/.zdraw-build-patch
if [[ ! -f $build_stamp ]]; then
  (
    cd "$build_root"
    patch -p1 < "$build_patch"
    autoconf
    autoheader
    if [[ -f config.status ]]; then
      ./config.status --recheck
      ./config.status
    fi
  )
  cp "$build_patch" "$build_stamp"
elif ! cmp -s "$build_patch" "$build_stamp"; then
  print -u2 -r -- 'Build integration patch changed; run make clean and retry.'
  exit 1
fi
if [[ ! -f $build_root/config.status ]]; then
  ( cd "$build_root"; ./configure --enable-dynamic )
fi
[[ -f $build_root/config.h ]] || {
  print -u2 -r -- 'Zsh configuration is incomplete; run make clean and retry.'
  exit 1
}
awk '$1 == "name=zdraw" { for (i = 2; i <= NF; i++) if ($i == "link=dynamic") ok = 1 }
     END { exit !ok }' "$build_root/config.modules" || {
  print -u2 -r -- 'Zsh must enable dynamic zdraw; install curses development headers/libraries, then run make clean and retry.'
  exit 1
}
# Build the shell as well, so tests have a host with the same configuration/ABI.
# Building Src avoids the documentation-generation toolchain.
"$make_command" -C "$build_root/Src"
typeset module_extension
module_extension=$(awk '$1 == "DL_EXT" && $2 == "=" { print $3; exit }' "$build_root/Src/Makefile")
[[ -n $module_extension && -f $build_root/Src/Modules/zdraw.$module_extension ]] || {
  print -u2 -r -- 'Zsh did not produce a loadable zdraw module.'
  exit 1
}
mkdir -p "$project_root/.build/modules"
cp "$build_root/Src/Modules/zdraw.$module_extension" "$project_root/.build/modules/"
mkdir -p "$project_root/.build/modules/zsh"
typeset helper
for helper in zselect system parameter zutil mathfunc; do
  if [[ -f $build_root/Src/Modules/$helper.$module_extension ]]; then
    cp "$build_root/Src/Modules/$helper.$module_extension" "$project_root/.build/modules/zsh/"
  fi
done
# Interactive experiments must use ZLE modules with this shell's ABI as well.
for helper in zle zleparameter complete; do
  if [[ -f $build_root/Src/Zle/$helper.$module_extension ]]; then
    cp "$build_root/Src/Zle/$helper.$module_extension" "$project_root/.build/modules/zsh/"
  fi
done
mkdir -p "$project_root/.build/functions"
cp "$source_root/Functions/Misc/add-zle-hook-widget" "$project_root/.build/functions/"
print -r -- "Built $project_root/.build/modules/zdraw.$module_extension"

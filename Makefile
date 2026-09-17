ZSH_BIN ?= zsh
PYTHON ?= python3

.PHONY: build test clean patch

build:
	+ZDRAW_MAKE="$(MAKE)" "$(ZSH_BIN)" -df scripts/build.zsh

test: build
	"$(ZSH_BIN)" -dfn lib/zdraw-text-selection.zsh
	"$(ZSH_BIN)" -dfn examples/text-selection.zsh
	"$(ZSH_BIN)" -dfn tests/text-selection.zsh
	"$(ZSH_BIN)" -dfn tests/text-selection-draw.zsh
	"$(ZSH_BIN)" -dfn tests/text-selection-example.zsh
	"$(ZSH_BIN)" -dfn benchmarks/text-selection.zsh
	"$(ZSH_BIN)" -dfn examples/held-keys.zsh
	"$(ZSH_BIN)" -dfn tests/held-keys.zsh
	"$(ZSH_BIN)" -dfn tests/raster.zsh
	"$(ZSH_BIN)" -dfn benchmarks/raster.zsh
	"$(ZSH_BIN)" -dfn benchmarks/raster-reference.zsh
	"$(ZSH_BIN)" -dfn examples/review-composition.zsh
	"$(ZSH_BIN)" -dfn tests/review-composition.zsh
	"$(ZSH_BIN)" -dfn examples/components/status-strip.zsh
	"$(ZSH_BIN)" -dfn examples/status-strip.zsh
	"$(ZSH_BIN)" -dfn tests/status-strip.zsh
	"$(ZSH_BIN)" -dfn examples/components/change-gutter.zsh
	"$(ZSH_BIN)" -dfn examples/change-gutter.zsh
	"$(ZSH_BIN)" -dfn tests/change-gutter.zsh
	"$(ZSH_BIN)" -dfn examples/components/linked-detail.zsh
	"$(ZSH_BIN)" -dfn examples/linked-detail.zsh
	"$(ZSH_BIN)" -dfn tests/linked-detail.zsh
	"$(ZSH_BIN)" -dfn examples/design-study.zsh
	"$(ZSH_BIN)" -dfn tests/design-study.zsh
	"$(ZSH_BIN)" -dfn scripts/design-study-frame.zsh
	"$(ZSH_BIN)" -dfn scripts/build.zsh
	"$(ZSH_BIN)" -dfn scripts/inline-shell.zsh
	"$(ZSH_BIN)" -dfn examples/inline-picker.zsh
	"$(ZSH_BIN)" -dfn tests/inline.zsh
	"$(ZSH_BIN)" -dfn tests/geometry.zsh
	"$(ZSH_BIN)" -dfn tests/drawing.zsh
	"$(ZSH_BIN)" -dfn tests/colorinfo.zsh
	"$(ZSH_BIN)" -dfn tests/cellinfo.zsh
	"$(ZSH_BIN)" -dfn tests/snapshot.zsh
	"$(ZSH_BIN)" -dfn tests/screen.zsh
	"$(ZSH_BIN)" -dfn lib/zdraw-screen.zsh
	"$(ZSH_BIN)" -dfn lib/zdraw-motion.zsh
	"$(ZSH_BIN)" -dfn tests/motion-monitor.zsh
	"$(ZSH_BIN)" -dfn scripts/replay-recipe.zsh
	"$(ZSH_BIN)" -dfn tests/fill.zsh
	"$(ZSH_BIN)" -dfn tests/copy.zsh
	"$(ZSH_BIN)" -dfn tests/restyle.zsh
	"$(ZSH_BIN)" -dfn tests/pads.zsh
	"$(ZSH_BIN)" -dfn tests/resizepad.zsh
	"$(ZSH_BIN)" -dfn tests/windows.zsh
	"$(ZSH_BIN)" -dfn tests/window-presentation.zsh
	"$(ZSH_BIN)" -dfn tests/pad-presentation.zsh
	"$(ZSH_BIN)" -dfn tests/spans.zsh
	"$(ZSH_BIN)" -dfn tests/truecolor.zsh
	"$(ZSH_BIN)" -dfn tests/text-policy.zsh
	"$(ZSH_BIN)" -dfn tests/textinfo.zsh
	"$(ZSH_BIN)" -dfn tests/textpos.zsh
	"$(ZSH_BIN)" -dfn tests/textwrap.zsh
	"$(ZSH_BIN)" -dfn tests/events.zsh
	"$(ZSH_BIN)" -dfn tests/session.zsh
	"$(ZSH_BIN)" -dfn tests/session-errors.zsh
	"$(ZSH_BIN)" -dfn tests/sgr.zsh
	"$(ZSH_BIN)" -dfn lib/zdraw-sgr.zsh
	"$(ZSH_BIN)" -dfn lib/zdraw-run.zsh
	"$(ZSH_BIN)" -dfn lib/zdraw-fixture.zsh
	"$(ZSH_BIN)" -dfn tests/visual.zsh
	@for file in lib/zdraw-ui.zsh lib/zdraw-panel.zsh lib/zdraw-list.zsh lib/zdraw-layout.zsh lib/zdraw-table.zsh lib/zdraw-tabs.zsh lib/zdraw-meter.zsh lib/zdraw-badge.zsh lib/zdraw-help.zsh lib/zdraw-input.zsh lib/zdraw-form.zsh lib/zdraw-document.zsh lib/zdraw-chart.zsh lib/zdraw-sparkline.zsh lib/zdraw-bars.zsh lib/zdraw-canvas.zsh lib/ui/*.zsh tests/ui*.zsh tests/gallery.zsh tests/composition.zsh tests/capabilities.zsh tests/enhanced.zsh tests/job-control.zsh scripts/portability/terminal.zsh scripts/portability/terminal-enhanced.zsh examples/capabilities.zsh examples/input-protocols.zsh examples/gallery.zsh examples/list-detail.zsh examples/table-inspector.zsh examples/task-monitor.zsh examples/form.zsh examples/document.zsh examples/canvas.zsh benchmarks/canvas.zsh; do \
	  "$(ZSH_BIN)" -dfn "$$file" || exit; \
	done
	"$(ZSH_BIN)" -dfn tests/presentation.zsh
	"$(ZSH_BIN)" -dfn tests/sync.zsh
	"$(ZSH_BIN)" -dfn tests/trees.zsh
	"$(ZSH_BIN)" -dfn tests/overlay.zsh
	"$(ZSH_BIN)" -dfn examples/stacking.zsh
	"$(ZSH_BIN)" -dfn examples/overlays.zsh
	"$(ZSH_BIN)" -dfn scripts/portability/frame-source.zsh
	"$(ZSH_BIN)" -dfn tests/unicode-edit.zsh
	"$(ZSH_BIN)" -dfn tests/unicode-draw.zsh
	"$(ZSH_BIN)" -dfn scripts/portability/unicode-source.zsh
	"$(ZSH_BIN)" -dfn tests/resources.zsh
	"$(ZSH_BIN)" -dfn benchmarks/components.zsh
	"$(ZSH_BIN)" -dfn tests/prepared.zsh
	"$(ZSH_BIN)" -dfn tests/clipping.zsh
	"$(ZSH_BIN)" -dfn benchmarks/spans.zsh
	"$(ZSH_BIN)" -dfn benchmarks/fill.zsh
	"$(ZSH_BIN)" -dfn benchmarks/native.zsh
	"$(ZSH_BIN)" -dfn tests/performance.zsh
	"$(ZSH_BIN)" -dfn examples/events.zsh
	"$(ZSH_BIN)" -dfn examples/streams.zsh
	"$(ZSH_BIN)" -dfn examples/hit-test.zsh
	"$(ZSH_BIN)" -dfn examples/wrapping.zsh
	"$(ZSH_BIN)" -dfn examples/borders.zsh
	"$(ZSH_BIN)" -dfn examples/colors.zsh
	"$(ZSH_BIN)" -dfn lib/zdraw-color.zsh
	"$(ZSH_BIN)" -dfn tests/color-helpers.zsh
	"$(ZSH_BIN)" -dfn examples/color-studio.zsh
	"$(ZSH_BIN)" -dfn examples/cell-inspection.zsh
	"$(ZSH_BIN)" -dfn examples/snapshot-diff.zsh
	"$(ZSH_BIN)" -dfn examples/regions.zsh
	"$(ZSH_BIN)" -dfn examples/copy.zsh
	"$(ZSH_BIN)" -dfn examples/restyle.zsh
	"$(ZSH_BIN)" -dfn examples/viewports.zsh
	"$(ZSH_BIN)" -dfn examples/windows.zsh
	"$(ZSH_BIN)" -dfn examples/truecolor.zsh
	"$(ZSH_BIN)" -dfn examples/clipping.zsh
	ZDRAW_MAKE="$(MAKE)" "$(PYTHON)" -m unittest discover -s tests -v

# Keep downloaded/extracted sources and other files under .build intact.
clean:
	rm -rf .build/zsh .build/modules .build/functions .build/source-root

# Export an additive Zsh integration patch, leaving zsh/curses untouched.
patch:
	@cat patches/zdraw-build.patch
	@for file in Src/Modules/zdraw.c Src/Modules/zdraw.mdd Src/Modules/zdraw_keys.awk Src/Modules/zdraw_grapheme.h Src/Modules/zdraw_grapheme_data.h Src/Modules/zdraw_raster.h Doc/Zsh/mod_zdraw.yo; do \
	  diff -u --label /dev/null --label b/$$file /dev/null $$file; result=$$?; \
	  test $$result -le 1 || exit $$result; \
	done

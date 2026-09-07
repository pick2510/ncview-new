# Bug: "time" dimension slider doesn't respond to mouse input (macOS)

## RESOLVED

**Root cause**: `core/src/util.cc`'s `add_var_to_list()`, in the branch that
merges an additional file into an already-known (multi-file/"virtual")
variable, accumulates the scan axis's total length into `var->size[0]`
(`var->size[0] += new_fdb->var_size[0]`) but never updated the *separate*
copy of that same length living in `var->dim[0]->size` (the `NCDim` struct
built once, from the first file only, by `fill_dim_structs()` back when the
variable was first created). `MainWindow::fillDimInfo()` sets the scan-axis
slider's range from exactly that stale `dim->size` (`bounds(0, size-1)`),
while `view_change_cur_dim()` (prev/next) and `set_scan_view()` (the
"frame N/M" label) both read the correctly-accumulated `var->size[0]`
directly. For a dataset split across many files with few timesteps per file
(e.g. the common WRF pattern of one file per output time), `dim->size`
stayed frozen at the first file's own tiny count -- in the worst case a
single timestep per file, giving `bounds(0, 0)`, a slider with zero range
that cannot move no matter how it's clicked or dragged -- while the buttons,
reading the real total, worked fine. This also explains why it wasn't
reproducible on Linux/Xvfb: those repros used a single file with multiple
timesteps inside it, never the multi-file case that actually triggers the
staleness.

Confirmed live on macOS: instrumenting `DimValueSlider::handle()` (see
below, still in the tree, gated behind `-debug`) showed `FL_PUSH`/`FL_DRAG`/
`FL_RELEASE` arriving correctly and `Fl_Slider::handle()` returning 1
(handled) throughout a real click-and-drag, but `value()` staying at `0`
across the whole gesture -- exactly what a `bounds(0,0)` slider does, and
inconsistent with every theory involving events being eaten by something
else.

**Fix**: `var->dim[0]->size` is now kept in sync with `var->size[0]` at the
same accumulation site. `tests/test_varlist.cc`'s multi-file test (which had
been asserting the stale value as intentional "port fidelity") was updated
to expect the two to match.


## Symptom

In the ncview FLTK port (repo: ncview-new, branch: master, as of commit
`abfca2c`), the main window has one row per dimension below the colorbar,
each row = `[name] [◀] [slider showing current value] [▶]`
(`ui/src/main_window.cc`, `MainWindow::rebuildDimRow()` /
`MainWindow::recenterDimRow()`, struct `DimRow` in
`ui/include/ncview_ui/main_window.h`).

On **macOS**, the slider in the **"time" row specifically** (the scan/animated
axis) does not respond to mouse input **at all**:
- A single click on the track: nothing happens (no jump to that position).
- Clicking directly on the little knob itself: nothing happens either.
- Dragging: nothing happens.
- Resizing the window afterward: no change, still completely unresponsive.

**The prev/next arrow buttons on the same "time" row work fine** and step
the dimension correctly. **Sliders on every other dimension row (e.g. a
"lev" row, or the "lat"/"lon" placeholder rows showing "-Y-"/"-X-") work
fine** — click-to-jump and drag both work as expected.

So it's isolated to: this one widget type (`Fl_Slider` subclass), on this
one specific row (the scan axis / "time"), on this one platform (macOS).

## What's been tried and ruled out

This was NOT reproducible on Linux under Xvfb with `xdotool` synthetic
mouse events (single clicks, and gradual multi-step drags with
mousedown/mousemove.../mouseup), tested repeatedly with:
- A file where "time" is the only extra (non-X/Y-axis) dimension.
- A file with both "time" and a "lev" dimension present together.
- Both from a cold start and after dragging other rows first.

In all of the above, dragging/clicking the time slider worked correctly
(value updated live, "frame N/M <date>" title label updated, 2-D image
redrawn) on Linux.

Two theories were tried and pushed, **neither fixed it** (confirmed by the
user after rebuilding from latest master each time):

1. **Commit `9034ce1`**: the slider was using `FL_WHEN_RELEASE` (actually
   just `Fl_Widget`'s own default, not an override of anything), so the
   displayed value/image didn't update until mouse-up. Changed to
   `FL_WHEN_CHANGED` so it updates live during drag. This fixed the "feels
   unresponsive during drag" UX issue in general, but the user then
   clarified the *actual* problem is narrower: the time slider doesn't
   respond to input **at all**, not even a single click, not even the
   commit-on-release case.

2. **Commit `abfca2c`**: `dim_pack_` was an `Fl_Pack` (auto-stacking
   container), and `MainWindow::layout()` read back each row's position
   via `row.group->x()/y()` right after resizing `dim_pack_`, to
   re-center the row's 4 children. Per FLTK's own `Fl_Pack::resize()`
   source, a pack does NOT reposition children when resized — only
   lazily, inside `draw()` — so that read could pick up a stale position.
   Replaced `dim_pack_` with a plain `Fl_Group` and made each row compute
   its own absolute position directly from `dim_pack_`'s (always-current)
   bounds plus the row's own stored index, instead of ever reading a
   sibling's position back. This was a real latent bug (confirmed by
   reading FLTK's source), but **the user reports it did not fix the
   actual issue** — resizing the window doesn't change anything either,
   which rules out this being a resize/timing-order issue at all.

## Relevant code

- `ui/include/ncview_ui/main_window.h`: `struct DimRow` (the slider is
  `Fl_Slider *value_slider`, declared as base-class pointer but actually
  constructed as a small custom subclass `DimValueSlider` — see below),
  `MainWindow` class private members (`dim_pack_`, `dim_rows_`).
- `ui/src/main_window.cc`:
  - `DimValueSlider` (anonymous namespace, just above
    `MainWindow::rebuildDimRow`): a small `Fl_Slider` subclass that
    overrides `draw()` to render its own `display_text_` (a formatted
    date/coordinate string) centered across the full widget, instead of
    relying on `Fl_Slider`'s own label rendering (which FLTK confines to
    the knob's own small rectangle — a separate, already-understood
    FLTK quirk, unrelated to this bug). It does **not** override
    `handle()` — mouse events go through the inherited `Fl_Slider::handle()`
    unmodified.
  - `MainWindow::rebuildDimRow(DimRow&)`: constructs one row's 4 widgets
    (`name_box`, `prev_btn`, `value_slider`, `next_btn`) inside a plain
    `Fl_Group`, positions them via `recenterDimRow()`, wires up callbacks.
  - `MainWindow::recenterDimRow(DimRow&)`: computes/sets the row's
    absolute position and re-centers its children.
  - `MainWindow::dimSliderCallback(Fl_Widget*, void*)`: the slider's
    callback, calls core's `view_set_cur_dim_index(name, place)`.
  - `MainWindow::setCurDimValue()` / `MainWindow::fillDimInfo()`: update
    the slider's bounds/value/display text whenever the dimension's
    current place changes (called from core via the `ncview/interface.h`
    seam) or when a new variable is selected.
  - `MainWindow::makeDimButtons(const Stringlist*)`: rebuilds all rows
    from scratch (`clearDimButtons()` then one `rebuildDimRow()` per dim)
    whenever the scannable-dimension list changes (e.g. new variable
    selected). Sets each row's `index` (0, 1, 2, ...) before building it.
  - `MainWindow::indicateActiveDim(Dimension, const char*)`: marks the
    currently-active dim's `name_box` bold. **The scan axis ("time") is
    presumably always the one marked bold/active** — this is the one
    structural thing that's actually different about the time row vs.
    others, though nothing here touches the slider itself, only
    `name_box`'s font.
- Core side (`core/src/view.cc`):
  - `view_apply_cur_dim_place()`, `view_change_cur_dim()` (relative
    +1/-1/+10% stepping, used by the prev/next buttons — this WORKS for
    time),
  - `view_set_cur_dim_index()` (absolute jump, used by the slider
    callback — added recently, only used by the slider),
  - `view_get_cur_dim_index()` (getter used to keep the slider's position
    in sync).
  - When the dim being changed is the scan axis (`dimid ==
    view->scan_axis_id`), `view_apply_cur_dim_place()` routes through
    `set_scan_view()` instead of the plain non-scan-axis path — this is
    the one core-side code path that's different for "time" vs. other
    dims, but it only runs *after* a value change is already decided; it
    can't be why the click/drag isn't even registering at the FLTK level.
  - There's also a recurring 1-second animation-related timer
    (`view_check_new_data`, armed via `in_timer_set` around
    `core/src/view.cc:690`/`742`/`791`) that re-arms itself while an
    animation is playing or a file might be growing. Not yet
    investigated as a possible interference source (e.g. if it somehow
    touches the time row's widgets while a mouse-down is in progress,
    it could in theory disrupt FLTK's internal drag-tracking state) —
    worth checking whether this timer is active in the user's session
    when they test (is playback active / paused / stopped?).

## What to investigate on the Mac

1. **Confirm it's really receiving no events at all**, not just
   swallowing them silently. Easiest: temporarily add an
   `fprintf(stderr, ...)` at the top of `DimValueSlider::draw()`'s
   sibling — actually more useful, override `int handle(int event)` in
   `DimValueSlider` (currently NOT overridden — it inherits
   `Fl_Slider::handle()` untouched) to log every event type it receives
   before calling `return Fl_Slider::handle(event);`, and see whether
   `FL_PUSH`/`FL_DRAG`/`FL_RELEASE` ever arrive for the time row's
   slider specifically, vs. an ordinary row's slider.
2. **Check for a genuinely overlapping widget.** Since this is
   isolated to exactly one specific row, on exactly one platform, with
   arrows on the *same* row working fine, the most likely explanation
   left is that *something else* (a widget this project doesn't intend
   to be there, or one from a stale rebuild) is sitting on top of the
   slider's screen rectangle for that row specifically and eating the
   click before FLTK's normal hit-testing (by z-order/creation order)
   would reach `value_slider`. Things to check:
   - Is `makeDimButtons()` (or `rebuildDimRow()`) ever called MORE than
     once for the same variable selection, potentially leaving two
     stacked `Fl_Group`s for the same row index if `clearDimButtons()`
     didn't fully run first? (`clearDimButtons()` is
     `dim_pack_->clear(); dim_rows_.clear();` — should be safe, but
     worth confirming with a debug print of `dim_pack_->children()`
     right after a rebuild.)
   - Is there anything in `ui/src/interface_fltk.cc` or `core/src/view.cc`
     that treats the scan axis specially and creates/shows an *additional*
     widget positioned at/near the time row (e.g. some
     animation-specific overlay) that isn't present for other rows?
     (Search for `scan_axis_id` usages in the UI layer, and anything
     that runs only when a dim `d->timelike` is true.)
   - Dump the actual FLTK widget tree at runtime (e.g. temporarily add
     a debug routine that walks `dim_pack_`'s children recursively,
     printing each widget's class name + `x(),y(),w(),h()`) right after
     opening a file, and compare the time row's group contents/bounds
     against a working row's.
3. **Check `Fl::pushed()` / `Fl::belowmouse()` state.** A stale internal
   FLTK pointer (e.g. left over from before the row was rebuilt, if
   something dangling still holds a reference) could in theory make
   `Fl_Group::handle()` misroute the event. This is a bit of a long
   shot but consistent with "resizing doesn't fix it" (a resize doesn't
   necessarily clear these caches) and "isolated to one specific,
   frequently-rebuilt row" (the scan axis row's controls might get
   rebuilt/recreated more often than others, e.g. every time the active
   dim changes, if `indicateActiveDim` or something nearby ever
   triggers a rebuild rather than just a font change — re-check that
   assumption directly).
4. Rule out anything animation/timer related: reproduce with playback
   definitely stopped/paused, and separately with it actively playing,
   to see if that changes anything.

## Repo / build info

- Repo: `ncview-new`, C++17 rewrite of the classic `ncview` netCDF
  viewer, FLTK-based UI (`ui/`), core ported from the original C
  (`core/`). Toolkit-neutral seam is `core/include/ncview/interface.h`;
  FLTK implements it in `ui/src/interface_fltk.cc` +
  `ui/src/main_window.cc`.
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake
  --build build -j`, binary at `build/app/ncview`.
- Latest relevant commits (most recent first): `abfca2c` (Fl_Group
  refactor, didn't fix it), `9034ce1` (FL_WHEN_CHANGED, didn't fix the
  real issue either, but is a legitimate separate improvement), `783662d`,
  `96bb6b1`, `c728b78` (earlier layout/slider work, for context).

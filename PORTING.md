# Porting plan: ncview to C++ / FLTK / CMake

## Context

`ncview` (upstream 2.1.11, ~29.6k lines of C) is a netCDF visual browser built on X11 + Xt + **Athena widgets (Xaw)**, with two vendored custom Xt widgets (`SciPlot.c`, 3.5k lines, and `RadioWidget.c`). It builds with autotools and optionally links the system `libudunits2`. Xaw is effectively dead upstream, the X-only rendering path blocks Wayland/macOS, and the autotools build is hard to maintain.

Goal: a self-contained C++ application on **FLTK**, built with **CMake**, with **FLTK and UDUNITS-2 vendored as git submodules** so the build has no toolkit or units-system dependency (only netCDF, plus expat for UDUNITS-2's XML parsing, remain external).

This is a **new, standalone repository** for the port — it does not carry any Debian packaging (that lives in the original Debian packaging repo, which is untouched by this work). The upstream C sources being ported live at `/home/strebdom/git/ncview` (read as reference only; nothing there is modified by this project).

### Decisions already taken
- **Port depth**: rewrite the UI layer in idiomatic C++/FLTK now; compile the existing core (`src/*.c`) as C++ with minimal changes, then modernize core module-by-module in a later phase.
- **udunits**: vendor upstream **UDUNITS-2 as a submodule** and build the *complete* library (all public `ut_*` / `cv_*` functions) from our CMake — no system `libudunits2`, no reimplementation.
- **FLTK**: vendored as a submodule, built via `add_subdirectory` and linked statically.
- **Packaging**: out of scope for this repo entirely.

## Why the port is tractable

The upstream `src/interface/interface.c` is already a deliberate toolkit seam: ~30 `in_*` functions (`in_initialize`, `in_draw_2d_field`, `in_set_label`, `in_button_pressed`, `in_dialog`, `in_timer_set`, …, declared in `src/ncview.protos.h`) that the core calls, each delegating to an `x_*` function in `src/interface/x_interface.c`. The port re-implements that same `in_*` contract over FLTK. Two leaks in the seam must be abstracted first:
- `in_timer_set()` takes an `XtTimerCallbackProc` → replace with a `std::function<void()>` + `Fl::add_timeout`.
- `src/ncview.includes.h` pulls X11/Xaw headers into *every* translation unit, core included → split into a UI-free core header.

## Repo layout

```
CMakeLists.txt              # top level: options, submodules, install rules
README.md                   # this project's readme
README, COPYRIGHT            # NOT ours — see README.md; hardcoded paths UDUNITS-2's CMake needs
third_party/
  fltk/                      # submodule -> github.com/fltk/fltk, pinned release-1.4.5
  udunits2/                  # submodule -> github.com/Unidata/UDUNITS-2, pinned v2.2.28
core/                        # ncview_core: no UI symbols at all
  include/ncview/*.h         # defines.h, protos.h, interface.h (the toolkit seam)
  src/                       # file.cc file_netcdf.cc view.cc util.cc overlay.cc do_buttons.cc
                              # do_print.cc stringlist.cc epic_time.cc handle_rc_file.cc
                              # calcalcs.cc utCalendar2_cal.cc udu.cc ncview.cc (ncview_main)
ui/                          # FLTK implementation of the in_* contract
  src/                        # interface_fltk.cc main_window.cc image_view.cc colorbar.cc
                              # plot_window.cc dialogs/*.cc colormap_funcs.cc
app/main.cc
data/                        # colormaps, overlays, man pages (copied from upstream repo root)
tests/
```

## Milestones

### M0 — Build skeleton (no behavior change) — done
- ✅ `third_party/fltk` submodule added, pinned to `release-1.4.5`.
- ✅ `third_party/udunits2` submodule added, pinned to `v2.2.28`. It ships its own working `CMakeLists.txt` — no wrapper needed. It finds **system EXPAT** via `FindEXPAT` (fatal error if absent, so `libexpat-dev` is a build dependency) and defaults `BUILD_SHARED_LIBS=ON`, overridden to `OFF` for static linking. Its CPack block and an `install(FILES ...)` rule hardcode `${CMAKE_SOURCE_DIR}/README`, `.../COPYRIGHT`, and `.../CHANGE_LOG`, which resolve to *our* repo root when it's pulled in via `add_subdirectory()`. **Post-v0.3.0**: rather than committing copies of those files at our root (where they read as this project's own and clutter the repo listing), the top-level `CMakeLists.txt` generates them there at configure time from the submodule's real copies, `.gitignore`'d rather than tracked (see `README.md`).
- ✅ Top-level `CMakeLists.txt`: C++17, `find_package(netCDF)` with a `pkg_check_modules(netcdf)` fallback, `add_subdirectory(third_party/fltk)` with tests/examples/fluid off and static libs, `option(NCVIEW_ENABLE_PNG)`.
- ✅ Placeholder `ncview_core` / `ncview_ui` / `ncview` targets and a `main.cc` smoke test that links FLTK + netCDF + UDUNITS-2 and opens a trivial window.
- Remaining: confirm a clean `cmake --build` from scratch (in progress), install the upstream `udunits2.xml` database under `share/ncview/udunits2/` and point `udu_utinit()` at it by default.

### M1 — Core as C++ (`ncview_core`) — done
- ✅ Copied `/home/strebdom/git/ncview/src/*.c` → `core/src/*.cc` (minus two files that turned out to be dead: `qsort.c` calls an undefined `sort()` and isn't in any Makefile.am — never built upstream; `geteuid.c` is a separate `noinst_PROGRAMS` diagnostic with its own `main()`, not part of `ncview` at all). Mechanical C++ fixes: `<string.h>`/`<strings.h>` added to `includes.h` (previously pulled in transitively by X11 headers), K&R-style function definitions converted to prototypes (`epic_time.cc`), a stray `String`/`XtTimerCallbackProc`/`XtPointer` use replaced, `PseudoColor` (an X11 visual-class constant `util.cc` compares `options.display_type` against) redefined locally as `#define PseudoColor 3` instead of pulling in X11. The pervasive `void *next/prev` linked-list fields (hundreds of implicit `void*`→`T*` conversions, legal in C, not in C++) are handled with `-fpermissive` on `ncview_core` rather than casting every site — flagged as a TODO to clean up properly later.
- ✅ Split `ncview.includes.h` into `core/include/ncview/includes.h`: stdio/netcdf/string only, no X11/Xaw/config.h. `HAVE_UDUNITS2` is now defined unconditionally by `core/CMakeLists.txt` instead of coming from an autotools-generated `config.h`.
- ✅ `core/include/ncview/interface.h` is the toolkit contract: the `in_*` functions (with `in_timer_set` now `void in_timer_set(std::function<void()>, unsigned long delay_ms)`) **plus 16 more functions core calls directly by name that are genuinely UI dialogs/state**, which upstream never routed through `in_*`: `set_options`, `printer_options[_init]`, `x_range`, `x_dataedit`, `x_seen_colormap_name`, `x_check_legal_colormap_loaded`, `x_create_colorbar`, `x_draw_colorbar`, `x_error`, `x_force_set_invert_state`, `x_init_dim_info`, `x_set_var_sensitivity`, `unlock_plot`, `get_persistent_X_state`, `pix_to_rgb`. ncview_ui (M3/M4) must implement all of these, not just `in_*`.
- ✅ Renamed upstream's `main()` in `ncview.c` to `ncview_main()` (declared in `protos.h`) — it was always meant to be the real entry point's driver, not literally `main`; `app/main.cc` (M3) will call it. This also happens to be what let `ncview_core` link into a test binary with its own `main` in the first place.
- ✅ **Headless stub checkpoint passed**: `tests/stub_interface.cc` implements every function in `interface.h` with no-op/trivial bodies. `tests/CMakeLists.txt` whole-archive-links `ncview_core` + the stub into a shared lib (`ncview_core_linkcheck`, no `main` of its own, so nothing to collide with) — this forces every single object file in `ncview_core` into the link, so *any* missing symbol anywhere in it fails here. It links clean. `core_headless_test` then exercises real core logic (calcalcs calendar math, stringlist, group-name parsing, string utils) linked normally against `ncview_core` and passes.
- Requires `CMAKE_POSITION_INDEPENDENT_CODE ON` (set at the top level) since the whole-archive shared-lib link needs `ncview_core` and `libudunits2` built as PIC.

### M2 — Units layer — done
- ✅ `udu.cc` and `utCalendar2_cal.cc` are unchanged logic, now bound to the vendored, statically-linked UDUNITS-2 (`HAVE_UDUNITS2` unconditional, set in `core/CMakeLists.txt`; the dead `#else` stub branch in `udu.cc` — no-op `udu_utistime`/`udu_calc_tgran`, passthrough `udu_fmt_time` — is simply never compiled now).
- ✅ XML path resolution in `udu_utinit()`: `ut_read_xml(path)` first, which itself already implements `$UDUNITS2_XML_PATH` → the installed-prefix default UDUNITS-2's own CMake baked in; as a last resort (only when both of those fail *and* the caller passed no explicit path) it retries against `NCVIEW_BUILD_TREE_UDUNITS2_XML`, a compile definition pointing at `third_party/udunits2/lib/udunits2.xml` — so `./build/tests/core_headless_test` and (later) `./build/app/ncview` work correctly straight out of an uninstalled build tree, with no env var and no `make install`.
- ✅ Verified end-to-end in `core_headless_test.cc`: `udu_utinit(nullptr)` + `udu_utistime()` against real unit strings, exercised with `UDUNITS2_XML_PATH` unset and no system-installed database present, so it's genuinely hitting the build-tree fallback and parsing the real vendored `udunits2.xml`.

### M3 — FLTK main window (the big one) — working end-to-end
Rewrote upstream's `src/interface/x_interface.c` (3.7k lines) as `ui/src/interface_fltk.cc` (free-function seam layer) + `ui/src/main_window.cc` (`MainWindow`, `ImageView : Fl_Widget`, `Colorbar : Fl_Widget`). Verified visually under Xvfb + ImageMagick `import`: opening a real (synthetic, ncgen-built) netCDF file shows the variable list, the 2-D color-contour field, colorbar with correct min/max, all the title/scanvar/extrema/blowup/transform labels, and per-dimension rows with prev/next step buttons — matching upstream's actual behavior for the same file.

Three real bugs surfaced during this verification, all fixed:
- **`options.blowup_default_size` was never set.** Upstream reads it from an X application-defaults resource (`Ncview*blowupDefaultSize: 300`, `fallback_resources.h`) inside `x_initialize()`; `view.c:calculate_blowup()` divides by it unconditionally. Left at its zero-initialized default, this is a divide-by-zero → `inf` → undefined float-to-int conversion, observed on this machine as `options.blowup` becoming `INT_MIN` and then a `malloc(INT_MIN*INT_MIN)` failure two calls later. Fixed by setting it (and the similarly-sourced, but actually-unused, `options.delta_step`) directly in `in_initialize()`.
- **udunits2 database path fallback needed to be environment-based, not per-call-site.** `utCalendar2_cal.cc` has its *own*, independent `ut_read_xml(NULL)` call (a separate `units_system` from `udu.cc`'s `unitsys`) — the M2 fix of retrying a specific path only inside `udu_utinit()` didn't cover it. Fixed by having `udu_utinit()` `setenv("UDUNITS2_XML_PATH", ..., 0)` (only if unset) instead, so every udunits2 entry point in the process benefits.
- **The first frame of a newly selected variable never drew.** Upstream's `set_scan_variable()` deliberately skips calling `change_view()` after `in_set_2d_size()` reports the image grew, with a comment explaining why: growing the Xt widget generates an X11 "expose" event that upstream's event loop wires back to `change_view()`. FLTK has no equivalent wiring in this port, so that redraw silently never happened. Fixed by having `in_set_2d_size()` call `change_view(0, FRAMES)` itself when the size increased, replacing the expose-event side channel with a direct call.

Also found during M3 (dead upstream prototypes, like `clip_i()`/`qsort_sl()` in M1): `in_make_dim_buttons()` and `in_clear_dim_buttons()` are declared but never called from any core file — the real dimension-panel entry points are `x_init_dim_info()` and `in_fill_dim_info()`. Dropped the dead ones from `ncview/interface.h`.

**Toolkit table used:**

| Existing (Xt/Xaw) | FLTK replacement |
|---|---|
| `XtAppMainLoop`, `XtAppAddTimeOut` | `Fl::run()`, `Fl::add_timeout` |
| `XCreateImage`/`XPutImage` for the 2-D field (`x_interface.c:456,1867,3554`) | `ImageView : Fl_Widget` — expand `ncv_pixel` (u8 index) through the colormap into an RGB buffer, `fl_draw_image`; keeps the existing `data_to_pixels()`/`expand_data()` core path untouched |
| Xaw `Command`/`Toggle`/`MenuButton`/`SimpleMenu` button bar | `Fl_Button`/`Fl_Toggle_Button`/`Fl_Menu_Button` in an `Fl_Pack`, driven by the existing `BUTTON_*` ids from `ncview.defines.h:44-65` |
| Xaw `Label` set via `in_set_label` | `Fl_Box`/`Fl_Output`, keyed by the same `LABEL_*` ids (`ncview.defines.h:84-104`) |
| `RadioWidget.c` (custom widget) | **deleted** — native `Fl_Round_Button` inside an `Fl_Group` |
| Xaw `List`/`Viewport` variable selector | `Fl_Scroll` of buttons, or `Fl_Tree` for the `--group`ed variable layout |
| App-defaults (`Ncview-appdefaults`, `fallback_resources.h`) | FLTK scheme + our own `~/.ncviewrc` handling, which `handle_rc_file.c` already implements |

Ship the colorbar (`cbar.c`) as a `Colorbar : Fl_Widget` in the same milestone — it shares the pixel/colormap path with `ImageView`.

### M4 — Plots and dialogs — done
Small modal dialogs added to `MainWindow` (`ui/src/main_window.cc`), each a plain `Fl_Window` run with the standard blocking-modal `set_modal(); show(); while(shown()) Fl::wait();` pattern, verified visually under Xvfb against the real data pipeline (not just "it compiles"):
- ✅ **Range dialog** (`x_range` → `MainWindow::rangeDialog`): shows the real global min/max, min/max float inputs, an "apply to all variables" checkbox.
- ✅ **Options dialog** (`set_options` → `MainWindow::setOptionsDialog`): checkboxes for `autoscale`, `want_extra_info`, `save_frames`, `auto_overlay`, reflecting and writing back the real `options` state, redrawing on OK.
- ✅ **Dimset dialog** (`in_set_scan_dims` → `MainWindow::scanDimsDialog`): two `Fl_Choice` populated from the real scannable-dims list, defaulting to the current X/Y axes; returns the new dim list in upstream's Y-then-X order.
- ✅ **Printer options dialog** (`printer_options` → `MainWindow::printerOptionsDialog`): device (printer/file radio), margins, font name/size/header-size, and all six include-checkboxes, all round-tripped through the real `PrintOptions` struct that `do_print.cc`'s PostScript writer consumes. `pix_to_rgb` returns real RGB from the active colormap (`MainWindow::pixelToRgb`) rather than echoing the pixel index, which that writer also needs.
- ✅ **Variable info popup** (`in_display_stuff`): an `Fl_Text_Display`/`Fl_Text_Buffer` window showing the variable's/global attributes text upstream already builds in `file_netcdf.cc`; owns itself, any number can be open at once (upstream's `MAX_DISPLAY_POPUPS` cap doesn't apply — no Xt widget-count pressure here).
- ✅ **Data-edit grid** (`x_dataedit`/`in_set_edit_place`): an `Fl_Table` (`ui/src/interface_fltk.cc`'s `DataEditTable`) backed directly by the `char**` cell-text buffer upstream hands over; click a cell to `fl_input()` a new value, which calls `view_change_dat()` and rewrites the cell in place. "Dump Data" calls `view_data_edit_dump()`. `in_set_edit_place()` drives `Fl_Table::set_selection`/`row_position`/`col_position` to highlight/scroll to a cell -- wired to the image view's middle-button press/drag (`set_dataedit_place()`), matching upstream's `Btn2Up`/`Btn2Motion` translation that this port had not yet wired up.
  - **v0.3.0**: "Dump Data" and the XY plot window's "Dump" now use a native `Fl_Native_File_Chooser` (`in_choose_save_file()`, a new seam function) instead of a plain text-input filename prompt. See `CHANGELOG.md`'s `[0.3.0]` entry for detail.
- ✅ **XY plots** (`SciPlot.c`/`plot_xy.c` → `ui/src/plot_window.cc`, `in_popup_XY_graph`): a `PlotWidget : Fl_Widget` implementing what `plot_xy.c` actually needs instead of porting the ~30-entry-point SciPlot API -- linear/log axes (auto or user-set range via small range sub-dialogs), up to `MAX_LINES_PER_PLOT` lines with markers/legend, mouse-position reporting (`view_report_position_vals`), an axis-dimension chooser (`view_set_XY_plot_axis`), "Locked" plot-reuse semantics matching upstream's single-locked-plot invariant (`unlock_plot`), a data dump, and PostScript export via `Fl_PostScript_File_Device`. Also newly wired: a plain left-click-release on the image pops up the plot (`plot_XY()`), Ctrl+click sets min/max from the value under the cursor (`set_min_from_curdata`/`set_max_from_curdata`) -- upstream's `Btn1Up`/`Ctrl<Btn1Up>`/`Ctrl<Btn3Up>` translations on `ccontour_widget`, previously unwired in this port.
- `in_error` / `in_dialog` already used `fl_alert` / `fl_input` / `fl_choice` since M3.
- ✅ **Overlay selection** (`set_options.c`'s overlay section, previously missing from `MainWindow::setOptionsDialog` entirely -- there was no UI path to pick an overlay at all until now, only the `NCVIEW_TEST_DIALOG=overlay` test hook calling `do_overlay()` directly): a radio-button group built from the real `overlay_names()`/`overlay_n_overlays()`/`overlay_current()`, plus a custom-overlay-file picker using **`Fl_Native_File_Chooser`** (`filesel.c`'s one real caller upstream -- it's used for exactly this, a custom overlay file, not for opening netCDF data files; `file_select()` itself has no other callers anywhere in upstream's own source). Seeded with `determine_overlay_base_dir()`. On OK, calls `do_overlay()` if the selection changed (or is "custom", matching upstream's own re-apply-on-custom condition in `set_options.c`).
  - `Fl_Native_File_Chooser` needs no new dependency: it's part of FLTK's core `fltk` library target already linked, not `fltk_images`.
  - Per-colormap enable/disable (the other thing `set_options.c` has) remains a deliberate scope gap -- see the M6 notes below on `colormap_funcs.c`.
- ✅ **Startup file-open chooser** (post-v0.2.0): launching with no input files used to just print "no displayable variables found!" and exit -- a dead end when started from a GUI (double-click, dock icon, "Open with") rather than a shell. `in_choose_input_files()` (new seam function, `ui/src/interface_fltk.cc`) pops a native `Fl_Native_File_Chooser` in `BROWSE_MULTI_FILE` mode when `ncview_main()` gets no files on the command line -- one dialog covers both opening a single file and picking a whole one-file-per-timestep run to open as a series, since core already merges however many filenames it's handed into one virtual variable.

Testing note: `ui/src/interface_fltk.cc`'s `in_initialize()` has env-var-gated test hooks (`NCVIEW_TEST_AUTOSELECT=1|<var name>`, `NCVIEW_TEST_DIALOG=range|options|dimset|info|dataedit|plot|print|overlay`, `NCVIEW_TEST_BUTTON=<name>`) used to drive the app under Xvfb without a real mouse/keyboard — harmless in normal use, worth keeping for regression checks. `print`'s hook defers to the first event-loop tick via `Fl::add_timeout(0.0, ...)` since `print_init()` (which seeds `PrintOptions`' defaults) runs after `in_initialize()` returns, not before.

### M5 — Output paths — done
- ✅ **Printing** (`do_print.c`): stayed as core logic essentially unchanged since M1 through v0.2.3 (`core/src/do_print.cc` hand-wrote PostScript and shelled out to `lpr` exactly like upstream's "Printer" device -- there was no native print dialog to replace, so `Fl_Printer` was considered new scope, not parity, and wasn't added). **v0.3.0**: replaced with `Fl_Printer`, on both the main and XY plot windows -- see `CHANGELOG.md`'s `[0.3.0]` entry and `modernization.md`'s "Post-v0.2.3: printing moved to `Fl_Printer`" section for what changed and why.
- ✅ **Fixed a real bug this uncovered**: `MainWindow::pixelToRgb` (`pix_to_rgb`) was returning plain 8-bit colormap values, but `do_print.cc`'s original PostScript writer -- ported verbatim from upstream, which expects X11 `XColor`-style 16-bit channels -- right-shifted every channel by 8 (`"%02x%02x%02x", (r>>8), (g>>8), (b>>8)`). Every printed/dumped pixel was silently coming out black. Fixed by scaling colormap bytes the same way X11 does (`value16 = value8*257`) in `pixelToRgb` instead of changing core's contract. This `>>8` contract survives the `Fl_Printer` rewrite (both `in_print()` and `dumpFrameToPng()` still expand through it), so `pixelToRgb` is unchanged.
- ✅ **PNG frame dump** (`-frames`/`options.dump_frames`, `in_draw_2d_field`): upstream's `x_interface.c` called libpng directly, gated behind `HAVE_PNG`. This port uses FLTK's own `fl_write_png` (`ui/src/interface_fltk.cc`'s `dumpFrameToPng`) instead of adding a new `NCVIEW_ENABLE_PNG`/libpng dependency -- FLTK already bundles libpng for its own image support (`fltk_images`), so `ncview_ui` just links that target and reuses `pix_to_rgb` to build the RGB buffer, same as the PostScript writer does.
- Verified under Xvfb: `-frames` produces `frame.NNNNN.png` files that visually match the on-screen render pixel-for-pixel (confirms the `pixelToRgb` fix); the printer-options dialog (previous commit) and the PNG writer share the exact same `pix_to_rgb` call, so the PostScript writer's colors are fixed by the same change.

### M6 — Parity, cleanup, docs — packaging/docs done; parity checklist partial
- ✅ **Install rules**: `install(TARGETS ncview)`, the supplementary `*.ncmap` files (copied from upstream's repo root into `data/colormaps/`, installed to `share/ncview/colormaps`), and `data/ncview.1` (copied from upstream's `data/ncview.1`, with one paragraph added noting this is the FLTK port). Verified end-to-end: `cmake --install build --prefix <scratch dir>` followed by running the installed binary under Xvfb against a real file.
- ✅ **`NCVIEW_LIB_DIR` wired up**: upstream's `init_display_interface()` (`core/src/ncview.cc`) already searched `NCVIEW_LIB_DIR` (an `#ifdef`, previously never defined in this port, so dead) plus `$NCVIEWBASE`/`$HOME`/`.` for extra colormap files; `core/CMakeLists.txt` now defines it to the installed colormaps path. (The colormaps installed there duplicate names already compiled in via `init_cmaps_from_data()` -- that's upstream's own behavior too, not new here.)
- ✅ **CPack**: a minimal `TGZ`/source-`TGZ` config in the top-level `CMakeLists.txt`. Note: UDUNITS-2's vendored `CMakeLists.txt` also calls `include(CPack)` itself (with its own `CPACK_*` variables) when pulled in via `add_subdirectory`; CMake's `CPack` module is include-guarded, so our later `include(CPack)` with our own `ncview`-named variables wins without conflict -- verified by configuring and checking no duplicate-target error.
- ✅ **`README.md`** fleshed out: prerequisites (netCDF, expat, X11 dev headers -- FLTK and UDUNITS-2 need nothing preinstalled, both build from the vendored submodules), build/run/install instructions, and a note about UDUNITS-2's default-install-prefix quirk (`CMAKE_INSTALL_PREFIX` defaults to this repo's parent directory unless overridden -- inherited from `third_party/udunits2/CMakeLists.txt`'s own default logic, not something this port's own `CMakeLists.txt` sets).
- **Button-bar parity pass**: added an `NCVIEW_TEST_BUTTON=<name>` test hook (`ui/src/interface_fltk.cc`, alongside the existing `NCVIEW_TEST_DIALOG`) that calls `in_button_pressed()` directly -- the exact path `MainWindow::buttonCallback` uses for a real click -- so every button can be regression-checked under Xvfb without a real mouse. Screenshotted blowup, blowup-type (bilinear/replicate), transform, invert-colormap, and invert-physical: all behave correctly.
  - **Found and fixed a real port bug this way**: `Colorbar::draw()` ignored both `transform_` and `options.invert_colors` entirely, always drawing a plain linear colormap gradient -- while upstream's `cbar.c:cbar_make()` applies the exact same transform-then-invert formula `util.cc:data_to_pixels()` uses for the image itself. So whenever a transform or "Invert Colormap" was active, the colorbar no longer matched what the image actually showed. Fixed by applying the identical formula in `Colorbar::draw()`; verified by screenshot comparison before/after (the gradient now visibly reverses/compresses in step with the image).
  - **Confirmed NOT a port bug**: pressing "Inv.Phys" made the `time` dimension row's displayed value fall back to a raw `"0"` instead of the formatted date. Traced to `view.cc:show_current_dim_values()` (called by `redraw_dimension_info()`), which -- unlike `view_change_cur_dim()` a few hundred lines away -- never checks `dim->timelike` before formatting, always just `snprintf("%lg", ...)`. Checked against upstream's `view.c`: byte-for-byte the same function, same missing check. This is upstream's own pre-existing inconsistency, not a porting regression, so left unfixed for parity rather than "fixed" beyond what upstream itself does.
  - **Found and fixed post-v0.2.3, a real gap missed by M3/M4**: upstream's `do_buttons.c` gives the transport buttons and the Range button `Modifier` semantics (Ctrl+click accelerates stepping; right-click on Range sets the range from just the current frame) that `MainWindow::buttonCallback` never wired up -- every click always passed `Modifier::M1`. See `CHANGELOG.md`'s `[0.3.0]` entry for what was restored and what deliberately wasn't (reverse Transform/colormap cycling).
- **Overlay rendering**: found and fixed two real bugs while getting this to render at all.
  - **Crash**: `util.cc:data_to_pixels()` unconditionally dereferences a global `ncv_pixel *pixel_transform` for every missing/fill-value pixel (`pix_val = *pixel_transform;`, not gated by `options.display_type` the way the other `pixel_transform` use a few lines down is). That global is only ever allocated in upstream's `interface/colormap_funcs.c` -- the X11 colorcell-allocation file this port deliberately never carries over (FLTK expands a pixel index straight to RGB, same as upstream's own `TrueColor` branch there) -- so it stayed `NULL` in this port and any frame with a missing value segfaulted. Confirmed via `gdb` backtrace. Fixed by allocating it as the same identity table upstream's `TrueColor` branch builds (`pixel_transform[i] = i`), in `core/src/ncview.cc:initialize_display_interface()` (mirroring where upstream builds it, right before the first colormap is created) -- this is pure core logic with no toolkit dependency, so it belongs in core, not in `ncview_ui`.
  - **Silently disabled**: `in_report_auto_overlay()` (M3's original stub) always returned 0. Upstream's equivalent, `x_report_auto_overlay()`, returns an X application-resource default (`Ncview*autoOverlay`, `app_data.auto_overlay`) that's ANDed with the live `options.auto_overlay` toggle in `view.cc:set_scan_variable()` -- a second, resource-file-only switch that defaults to 1 and that this port has no equivalent resource system to override. Returning 0 meant automatic coastline overlays could never trigger no matter what the Options dialog's checkbox said. Fixed to return 1 (upstream's compiled-in default).
  - Verified via a new `NCVIEW_TEST_DIALOG=overlay` hook (calls `do_overlay(OVERLAY_P8DEG, ...)` directly, bypassing the auto-detection extent heuristic, since none of the sandbox's test files have large enough geographic extent -- `>10`/`>60` degrees -- to trigger auto-overlay for real): renders without crashing and visibly marks overlay pixels on the image. The exact coastline *shape* wasn't separately checked against a large-extent file (none available in this sandbox), only that the marking mechanism itself now runs safely and produces visible output.
- **`.ncviewrc` round-trip**: verified against both a fresh `$HOME` (first run creates a valid minimal file: just the two header lines, no crash; a second run then reads it back with no "could not open" warning and leaves it byte-for-byte unchanged, matching the "only write if no state file was found" contract in `ncview.cc:ncview_main()`) and against a **real** pre-existing `.ncviewrc` from an actual upstream ncview install (found at `$HOME/.ncviewrc` in this sandbox, containing ~20 `CMAP_<name> INT 1` lines) -- read back with no error and left untouched.
  - **Known, deliberate scope gap, not a bug**: that real file's `CMAP_*` entries record per-colormap enabled/disabled state for cycling, written by `interface/colormap_funcs.c:colormap_options_to_stringlist()` and consumed by that same file's colormap-list matching against `read_in_state`. `colormap_funcs.c` was never ported (see M0's risk list: "colormap semantics... need genuine reimplementation, not translation" -- it's X11 colorcell-allocation logic with no FLTK equivalent), so this port has no per-colormap enable/disable UI at all yet (`MainWindow`'s colormap list has no such toggle), and `get_persistent_X_state()` returns `nullptr` rather than a `CMAP_*` list. Round-tripping is safe either way (the entries are simply ignored on read), but the enable/disable *feature* itself would be new scope, not a fix -- left for a future session rather than built unprompted here.

## Files intentionally not ported

`SciPlot.c/.h/P.h`, `RadioWidget.c/.h`, `fallback_resources.h`, `helvR08.h`, `*_bitmap.h`, `Ncview-appdefaults`, `install-appdef`, and the whole autotools set (`configure.in`, `Makefile.am/in`, `aclocal.m4`, `m4macros/`, `depcomp`, `missing`, `install-sh`). Roughly 6k lines of custom-widget code deleted rather than ported.

Also dropped, discovered during M1: `qsort.c` (calls an undefined `sort()`, not in any Makefile.am — dead code, never actually built upstream) and `geteuid.c` (a separate `noinst_PROGRAMS` diagnostic executable with its own `main()`, unrelated to the `ncview` binary).

## Post-port modernization

Once the M0-M6 port above landed, a second, separate effort modernized
`core/`'s internals to idiomatic C++17 on the `modernization` branch (see
`modernization.md` for the full phase-by-phase log), under a strict-parity
rule: every commit had to leave `ctest` output and the Xvfb UI screenshot
harness byte-identical to the pre-change build. In order:

- **Phase 0**: built the safety net first -- `-Wall -Wextra` baseline
  (`core/WARNINGS.md`), characterization tests, an Xvfb screenshot harness
  driving the real binary headlessly, and an ASan/UBSan CI job.
- **Phases 2-4**: bounded `strcpy`/`strcat` (`snprintf`), `Stringlist`'s
  intrusive linked list to `std::vector`, and `char*`-returning core
  functions to `std::string`.
- **Phase 5**: `NCVar`/`FDBlist`/`NCDim`/`NCDim_map_info`'s intrusive lists
  to owning `std::vector<std::unique_ptr<...>>` containers -- the largest,
  highest-risk phase, ~1650 lines across 20 files.
- **Phase 6**: `View`/`FrameStore`/`Options`/`OverlayOptions`/`PrintOptions`'
  raw buffers (`malloc`/`realloc`/fixed `char[]`) to `std::vector`/
  `std::string`/`std::unique_ptr`.
- **Phase 7**: the `interface.h`/`protos.h` seam -- `char*` parameters that
  are only ever read became `const char*`/`std::string_view`, and
  `BUTTON_*`/`LABEL_*` became `enum class Button`/`Label`.

**What this deliberately left alone**: `core/`'s 181 raw `exit()` calls
(this codebase's uniform error-handling convention -- an unrecoverable
condition terminates the process outright, both in original upstream code
and in every phase of this modernization; no exception-based error handling
was introduced) and, *at the time*, its 5 global variables (`options`,
`variables`, `pixel_transform`, `framestore` in `ncview.cc`, `view` in
`view.cc`) -- each already documented at its declaration as the seam
through which `core` and `ncview_ui` share state, and none of the seven
phases found a reason to eliminate them under this phase's strict-parity
rule. **This decision was revisited** in the `OOP_redesign` branch (see
below) once the goal shifted from "port with zero behavior change" to
"maintainability" -- a different bar, since ongoing changes (not just this
one-time port) are what ownership boundaries actually pay for. `exit()`
usage was not revisited and remains this codebase's error-handling
convention.

**Defects found along the way** (full detail in `modernization.md`'s
"Sanitizer findings" and per-phase sections): an ASan-caught
heap-buffer-overflow off-by-one in `view_data_edit()` (fixed, with a
regression test); a segfault in `add_var_to_list()` from a Phase 5
draft's field-initialization ordering (caught by the Xvfb smoke test, not
the unit suite, before it ever reached this branch's history); a
pre-existing `-cal` argument allocation-size bug (fixed in Phase 2, inline
with the `strcpy`->`snprintf` conversion that depended on it); and one
upstream defect in `fill_dim_structs()`'s unit-mismatch check (an
infinite-loop-shaped `while` condition), originally preserved verbatim
per the strict-parity rule this phase held to -- since fixed post-v0.2.0
(see "Post-v0.2.0 defect audits" below), once it stopped being purely
mechanical-conversion work and became a real external code-review pass
with its own license to fix genuine bugs rather than only preserve them.

## OOP_redesign: turning the procedural core into explicit classes

A third effort, on the `OOP_redesign` branch, revisited the "5 globals
stay globals" decision above -- this time explicitly for maintainability
rather than parity, per the plan document each step's commit references.
Unlike the modernization phases, this was not a strict-parity conversion:
each step still had to leave `ctest`, `ncview_core_linkcheck`, and the
Xvfb screenshot harness passing, but the goal was real ownership, not
preserving every implementation detail.

Nine steps, each its own commit with full verification:

1. Extended `tests/stub_interface.cc` with call recording and scripted
   dialog responses, so later steps had a regression net for workflows
   (variable selection, range cancellation, playback) that had none.
2. Extracted `FrameRenderer` (`core/include/ncview/frame_renderer.h`) --
   the pure pixel-mapping loop -- out of `data_to_pixels()`.
3. Extracted `FrameCache` (`core/include/ncview/frame_cache.h`) from the
   bare `FrameStore` struct, replacing direct field access with named
   methods (`reset`, `growTo`, `invalidateAll`, `lookup`, `store`).
4. Dissolved `interface_glue.cc` -- its four functions moved to the files
   that actually own the logic they wrap (`view.cc`, `do_buttons.cc`,
   `util.cc`).
5. Introduced `Dataset` (`core/include/ncview/dataset.h`) and a
   `NetCDFFile` RAII wrapper, closing files for the first time in this
   codebase's history (`fi_close()` previously had zero callers -- every
   opened file leaked open for the process's life). `FDBlist::id` (a raw
   `int`) became `FDBlist::file` (a non-owning `NetCDFFile*`) with an
   `id()` accessor.
6. Wrapped the global `View*` in a `std::unique_ptr<ViewState>`
   (`ViewState` is an alias for `View`), fixing two real leaks this
   surfaced: `set_scan_variable()`'s early-return path, and
   `invalidate_variable()`'s reset -- both used to reassign/clear the
   pointer without deleting what it pointed to.
7. Introduced `ViewerController` (`core/include/ncview/viewer_controller.h`),
   absorbing `do_buttons.cc`'s 21 `do_*()` actions as named methods, with
   playback state (`cur_button_`) as a real member instead of a file-static.
8. Introduced `ViewerSession` (`core/include/ncview/viewer_session.h`),
   which became the real owner of `Dataset`, the active `ViewState`, and
   `FrameCache`.
9. Moved every field of the global `Options` struct onto `ViewerSession`,
   grouped into `RenderSettings`/`PlaybackSettings`/`SessionDisplayPrefs`/
   `StartupSettings`, and converted the `interface.h` free-function seam
   into a `ViewerUi` virtual interface (`core/include/ncview/viewer_ui.h`)
   with two concrete implementations: `FltkViewerUi` (the real app) and
   `RecordingViewerUi` (tests).

**The result of these nine steps**: `options`, `variables`, `g_dataset`,
`view`, `framestore`, and `pixel_transform` were no longer 6 independent
globals -- they were all facets of one object, the global `ViewerSession
g_viewer_session`. `Options`'s ~40 fields were reference members bound to
`ViewerSession`-owned storage; `g_dataset`/`view`/`framestore`/
`pixel_transform` were references onto `ViewerSession`'s own
`Dataset`/`ViewState`/`FrameCache`/pixel-index-table members. The bridge
names themselves were still there, deliberately, at that point -- this was
the load-bearing design choice that made each step safely verifiable on
its own (the same technique Phase 5's `Dataset`/`variables` migration
pioneered): move *ownership*, not *every call site*, per step.

### Removing the bridges: what turned out to be actually possible

A follow-up pass tried to go further and asked whether the bridge names
themselves -- and the separate `g_viewer_session`/`g_viewer_controller`/
`g_viewer_ui` composition-root globals that steps 7-9 introduced --
could be removed entirely, with every call site reading application
state through an explicitly-injected reference instead of a global (the
full dependency-injection shape the plan's original architecture diagram
showed).

**Investigation found this isn't achievable without a much larger, separate
rewrite.** `do_range()`, `do_pause()`, `in_button_pressed()`, and
`viewer_ui_bridge.cc`'s ~50 `interface.h` forwarder functions are free
functions with fixed signatures (the `interface.h` seam contract, and
`do_buttons.cc`'s action names), called from `view.cc`, `do_print.cc`,
`ui/src/interface_fltk.cc`'s test hooks (`NCVIEW_TEST_DIALOG`/
`NCVIEW_TEST_BUTTON`), and `tests/`. A fixed-signature free function has
exactly one way to reach an object instance in C++: global or static
state. Eliminating application-level globals entirely would mean
rewriting that free-function seam itself into something else (e.g. every
FLTK widget callback carrying an explicit context pointer, plumbed down
through dozens of call sites) -- a materially larger and riskier
undertaking than anything in the nine steps above, and a distinct piece
of work from removing *redundant* bridge names.

**What was actually done instead**: collapse the three differently-named
composition-root globals (`g_viewer_session`, `g_viewer_controller`,
`g_viewer_ui`) into one `AppContext` (`core/include/ncview/app_context.h`)
holding a `ViewerSession`, a `ViewerController`, and a non-owning
`ViewerUi*`, plus fold `pixel_transform` into `ViewerSession` (it had been
the one field left out of step 9). Every bridge name now resolves through
this single `g_app` global instead of three separately-named ones. This is
an honest, permanent design choice, not leftover migration debris: a
single, clearly-named composition root is what a C-style callback/seam
architecture like this one's realistic floor looks like, and further
"purging" would only relocate the same global under `ui/`'s existing
`MainWindow` singleton (itself unrelated, pre-existing, and out of scope)
rather than eliminate anything.

This pass also fixed a static-initialization-order bug it introduced along
the way: `g_viewer_ui` used to be a bare pointer, constant-initialized to
`nullptr`, so a static initializer in any other translation unit could
safely assign to it regardless of construction order. Folding it into
`AppContext` meant the whole aggregate now requires *dynamic*
initialization (`ViewerSession`/`Dataset` have non-trivial members), which
reintroduced the classic ordering hazard: `tests/stub_interface.cc`'s
former static-initializer trick for installing the `RecordingViewerUi`
could run before `g_app`'s own constructor, which would then silently
reset `g_app.ui` back to `nullptr`. Fixed by moving that assignment into
an explicit `installRecordingViewerUi()` call from each test binary's
`main()`, which is guaranteed to run after all static initialization
completes.

### Follow-up: pulling ownership logic out of util.cc and view.cc

Two further passes revisited files the nine steps above deliberately left
as grab-bags of free functions, moving the pieces that actually own or
mutate state onto the class that should own it, in each case updating
every call site directly rather than keeping a compatibility bridge.

**`util.cc`** mixed genuine math/string helpers with the functions that
build and mutate the variable list -- exactly the logic the original
`Dataset` design section said should move onto `Dataset` ("`add_var_to_list()`'s
existing virtual-multi-file-append logic... moves onto `Dataset` verbatim"),
deferred at step 5 to keep that step's diff reviewable. Moved onto
`Dataset` as real methods: `addVariable`/`addVariables` (was
`add_var_to_list`/`add_vars_to_list`), `findVariable` (was `get_var`),
`cacheScalarCoordInfo`, `calcDimMinmaxes`, `initMinMax`, and
`getMinMaxOnestep` (kept *public*, not private to `initMinMax`, because it
turned out to have a second caller in `view.cc`'s `view_check_new_data()`
-- found by grepping every call site rather than trusting the original
file's layout). Their file-private helpers (`check_ranges`,
`copy_info_to_identical_dims`, `equivalent_FDBs`, `new_fdblist`) moved
with them as `Dataset` implementation details. Left as free functions,
since they only fill in an already-allocated `NCVar*`'s fields without
touching the variable list (or, for `virt_to_actual_place`, belong to the
lower file-IO layer `Dataset` itself wraps): `fill_dim_structs`,
`handle_dim_mapping` (now non-static, since `Dataset::addVariable()`
needs it), `is_scannable`, `n_vars_in_list`, `get_group_list`,
`virt_to_actual_place`.

**`view.cc`** (~3300 lines, ~60 functions) is far more entangled: unlike
`util.cc`'s clean split, most of its functions mix state mutation with
direct `in_*`/`x_*`/`do_*` calls (dialogs, timers, cursor state, label
updates) in the same function body -- exactly what step 6 above deferred
("No dialogs, timers, or label updates -- those are the controller's
job"). A function-by-function scan (grepping every function body for a
direct UI call) found 11 functions that were both (a) genuinely pure
state -- zero direct UI calls -- and (b) already `static` (file-local to
`view.cc`, no external callers at all), making them a uniquely low-risk
subset: `determine_scan_axes`, `initial_determine_scan_axes`,
`re_determine_scan_axes`, `fill_view_data`, `alloc_view_storage`,
`init_view`, `set_scan_place`, `initial_set_scan_place`,
`re_set_scan_place`, `calculate_blowup`, and `view_data_has_missing`.
These moved onto `View`/`ViewState` (`core/include/ncview/defines.h`) as
member functions -- `determineScanAxes`, `setScanPlace`,
`calculateBlowup`, `allocStorage`, `fillViewData`, `hasMissingData`
public; `initialDetermineScanAxes`, `reDetermineScanAxes`,
`initialSetScanPlace`, `reSetScanPlace` private, since each had no caller
outside the one public method it now belongs to -- and `init_view`
(which allocated a new `View`) became the static factory `View::create()`.
Adding member functions to `View` doesn't disqualify it from remaining an
aggregate (`tests/test_pixels.cc`'s `View view{};` keeps compiling): C++17
only bars user-declared constructors, virtual functions, and
private/protected *data* members from an aggregate, not member functions.

**Phase 2** widened this to functions that are *also* file-local with no
external callers and state-mutating, but each makes exactly one direct
UI call as part of that mutation, rather than zero: `set_scan_buttons`
(`View::setScanButtons`), `view_set_axis` (`View::setAxis`),
`show_current_dim_values` (`View::showCurrentDimValues`),
`label_dimensions` (`View::labelDimensions`), and `flip_if_inverted`
(`View::flipIfInverted`). The UI call stays inline in the method body
rather than being split out to a separate controller-side call -- the
same precedent `Dataset::checkRanges` already set for calling
`in_dialog()` directly from a state method when the call is small and
unconditionally part of the operation, not something a caller might want
to skip or intercept.

**Phase 3** revisited the premise behind stopping at Phase 2. The earlier
assumption was that a function mixing state with *several* UI calls, or
with dialog/timer logic woven through its control flow, was too risky to
move without first splitting it into a state half and a UI half --
genuinely risky surgery. But moving a function's body *unmodified* --
however many UI calls, branches, or early returns it contains -- onto
`View` as a method carries none of that risk, provided the one property
that actually matters holds: **the function has no `if (view == NULL)
...` guard of its own.** A guard like that is load-bearing wherever the
global `view` can genuinely be null when the function is reached (an
expose event before any variable is selected, a mouse click on an empty
2-D pane) -- calling a method through a null `unique_ptr` there would be
undefined behavior, so those functions must stay free. Every other
function -- regardless of size or how many dialogs/timers/UI calls it
makes -- converts exactly as mechanically and safely as Phase 1/2's
simplest cases: rename it, add a `View *view = this;` (or `const View
*view = this;`) alias so the body's existing `view->foo` text needs no
further edits, fix call sites. No control flow, branching, or call
ordering changed anywhere in Phase 3.

This was verified per function by reading its body and tracing every
call site -- not assumed from size or name. It moved 18 more functions
onto `View`: `view_apply_cur_dim_place` (`applyCurDimPlace`),
`view_set_scan_dims` (`setScanDims`), `set_scan_view` (`scanToPlace` --
distinct from Phase 1's `setScanPlace`: this jumps to an absolute frame
during navigation/playback, not the one-time initial axis/place setup on
variable switch), `view_change_blowup` (`changeBlowup`), `view_set_range`
(`setRange`), `view_set_range_frame` (`setRangeFrame`), `set_range_labels`
(`setRangeLabels`), `init_saveframes` (`initSaveframes`),
`set_dataedit_place` (`setDataeditPlace`), `view_data_edit` (`dataEdit`),
`view_change_dat` (`changeDat`), `view_data_edit_dump` (`dataEditDump`),
`plot_XY_sc` (`plotXYSc`), `view_set_XY_plot_axis` (`setXYPlotAxis`),
`view_plot_XY_fmt_x_val` (`plotXYFmtXVal` -- dead code, no callers
anywhere in the tree, moved anyway since it cost nothing), `view_information`
(`information`), `redraw_dimension_info` (`redrawDimensionInfo`), and
`view_check_new_data` (`checkNewData`). External call sites updated
directly in `viewer_controller.cc`, `ui/src/interface_fltk.cc`,
`ui/src/main_window.cc`, `ui/src/plot_window.cc`, and
`tests/test_view_data_edit.cc`.

One real wrinkle surfaced along the way: `checkNewData`'s own
no-capture timer lambda referred to `view` expecting the *global*, but
once inside a method the local `View *view = this;` alias shadows it for
unqualified lookup, so the lambda failed to compile ("view is not
captured"). Fixed by qualifying that one reference as `::view` to reach
the global explicitly -- the lambda still runs later, when the timer
fires, and still reads whatever the global `view` is at that time,
identical to the pre-conversion behavior.

**What's left** (~22 functions, ~1400 lines) stays free, each for one of
four concrete reasons, not a general "too risky" judgment:
- **Has its own load-bearing `view == NULL` guard**, reachable from a
  context where that's genuinely possible: `view_draw`, `change_view`,
  `view_current_nt`, `view_change_cur_dim`, `view_set_cur_dim_index`,
  `view_get_cur_dim_index`, `invalidate_all_saveframes`,
  `view_report_position`, `set_min_from_curdata`, `set_max_from_curdata`,
  `plot_XY`, `view_recompute_colorbar`, `view_construct_scalar_coord_str`.
- **Destroys the very object a method's `this` would refer to**:
  `invalidate_variable` (calls `view.reset()`).
- **Isn't actually View-shaped** despite living in `view.cc`: `set_buttons`
  (pure UI button-widget state), `draw_file_info` (primarily `NCVar*`-shaped),
  `view_change_transform` (an `Options`-shaped render setting, touches no
  `view->` field), `view_report_position_vals` (reads a file-static array,
  not `view`), `mouse_xy_to_data_xy`/`view_get_scaled_size`/
  `view_calc_minval_float`/`view_calc_maxval_float`/`strip_trailing_zeros`
  (pure math/string helpers -- "math helpers stay free functions", per
  the original plan's own rule).
- **Trivial wrapper or entry point, not worth a method's ceremony**:
  `redraw_ccontour` (one line, forwards to `view_draw`), `view_data_edit_warn`
  (one dialog then forwards to `dataEditDump`, touches no `view->` field
  itself), `set_scan_variable`/`in_variable_selected` (the entry points
  that construct/replace the view in the first place -- more naturally
  free-function orchestrators than methods on the object they're
  building).

## Refine the architecture and deepen the test suite (Phase 0a)

The nine `OOP_redesign` steps and the `util.cc`/`view.cc` follow-up above
fixed *ownership* in the files they touched, but most of `core/src` is
still upstream's free-function style, and the test suite (72 doctest
cases against ~16,000 lines of `core/`) hadn't kept pace. A new,
larger plan picks that up (see the session notes for the full phase
breakdown); this entry covers its first landed piece, Phase 0a: test
state isolation.

Every doctest `TEST_CASE` in `ncview_core_tests` runs in the same
process, sharing one `g_app`. Until now that was managed by convention
(`test_varlist.cc`'s "each test needs a unique variable name" rule), which
doesn't scale as later phases add many more test cases that select
variables and open files. `tests/support/session_fixture.h` adds
`SessionFixture`, an RAII type that move-assigns a fresh
`ViewerSession()` into `g_app.session` in place (preserving every bridge
reference bound to its sub-objects) and clears the UI-recording double's
state, on both construction and destruction -- so a `TEST_CASE` gets a
clean `Dataset`/`View`/`FrameCache` regardless of what ran before it, and
one that throws mid-test still restores it on unwind. `tests/support/
scratch_home.h` factors the `$HOME`-redirect helper `test_rcfile.cc` had
already built (`ScratchHome`) out into shared infrastructure. Proven by
`tests/test_session_fixture.cc` and by re-running the whole suite under
`--order-by=rand` across 8 seeds with no order dependence.

Building this surfaced two real, previously-invisible bugs, both because
`SessionFixture` is the first code path that ever destroys a live
`Dataset` mid-process -- every previous test run left teardown to process
exit, and `tests/main.cc` deliberately calls `std::_Exit()`/
`TerminateProcess()` (`fast_exit.h`, to route around an HDF5-cleanup hang
on Windows CI) specifically to *skip* static destructors:

- **`initialize_misc()` (`ncview.cc`) was two responsibilities welded
  together**: `udu_utinit(NULL)` (unsafe to call a second time --
  `test_udunits_helper.h` documents why) and everything else
  (`options.*` defaults, allocating `options.overlay`, resetting
  `framestore`) -- the latter is exactly what per-test isolation needs to
  re-run and the former is exactly what it must not. Split into
  `reset_session_defaults()` (idempotent, called by both
  `initialize_misc()` and `SessionFixture`) and a slimmed
  `initialize_misc()` that calls `udu_utinit(NULL)` once, then it.
- **`new_netcdf()` (`util.cc`) allocated a `NetCDFOptions` with `malloc()`,
  but its one caller (`Dataset`'s `new_fdblist()`) hands the result to a
  `std::unique_ptr<NetCDFOptions>`**, whose default deleter calls
  `delete`. A real malloc/delete mismatch, caught immediately by ASan's
  alloc-dealloc-mismatch check once a `Dataset` was actually torn down
  mid-run. Fixed by allocating with `new` instead.

Both are one-line-cause, narrowly-scoped fixes to code this phase didn't
otherwise touch -- found and fixed because the new isolation
infrastructure finally exercised a destruction path nothing had before,
not because either was being hunted for.

## Post-v0.2.0 defect audits

Four rounds of external code review against the released `v0.2.x` builds
found and fixed ~16 further defects across `core/` -- most upstream bugs
carried through the port verbatim (the M1-M9 phases' strict-parity rule
deliberately preserved them; these audits are what finally closed them
out), a few genuine port regressions. **Full list: `CHANGELOG.md`'s
`[0.2.1]`-`[0.2.3]` entries**; `modernization.md`'s own "Post-v0.2.0
defect audits" section has the same pointer rather than a second copy.

## Verification

1. **Build**: `git submodule update --init --recursive && cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j` — must succeed with no system FLTK/udunits2 installed.
2. **Headless core**: `ctest --test-dir build` runs the M1 stub-interface tests (file open, `data_to_pixels` output hashes, `udu_fmt_time` formatting across the calcalcs calendars, `.ncviewrc` parse) — these must match the original Xaw build's output byte-for-byte (built from `/home/strebdom/git/ncview`).
3. **Units parity**: a test that parses the full bundled `udunits2.xml` and round-trips a table of unit strings/conversions through `ut_parse`/`cv_convert_double`, compared against system `libudunits2` where installed.
4. **Interactive**: `./build/app/ncview <file>.nc` on a real dataset; walk the M6 parity checklist, comparing screenshots against the original ncview binary.
5. **Leak/UB pass**: `-fsanitize=address,undefined` build over the headless tests — the mechanical C→C++ conversion of 14k lines is exactly where this pays off.

## Risks

- **Scale**: ~10k lines of UI to rewrite; M3+M4 are the bulk of the work. M0–M2 are days; M3–M4 are the multi-week part.
- **Static FLTK + X11**: linking still needs X11 dev headers on Linux (FLTK's backend); Wayland support requires FLTK 1.4 built with `FLTK_BACKEND_WAYLAND=ON`.
- **Colormap semantics**: the old code allocates X colormap cells and does index-based animation; FLTK has no colormap concept, so `colormap_funcs.c` and the invert/blowup paths need genuine reimplementation, not translation.

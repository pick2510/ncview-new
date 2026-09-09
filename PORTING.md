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

### Phase 0b (partial): a real fake timer, and playback tests

`stub_interface.cc`'s `RecordingViewerUi::in_timer_set()` used to just
record its own name and drop the `std::function` callback core handed it
-- meaning nothing had ever actually driven playback
(`ViewerController::rewind`/`fastforward`, whose `Modifier::M1` paths
only advance the movie because their timer callback re-arms itself and
steps again) or the file-growth poll (`View::checkNewData()`) under test.
`stub_interface.cc` now keeps the pending callback in a real one-shot
queue (`g_pending_timer_callback`/`g_timer_armed`), with `fireTimer()`
taking ownership of the callback and clearing the armed flag *before*
invoking it -- reproducing genuine one-shot timer semantics, so a
callback that re-arms itself (as every real one does) doesn't step on
the timer being fired. `timerIsArmed()`/`timerDelayMs()`/`fireTimer()`
are exposed via `tests/support/session_fixture.h`, and
`resetStubRecording()` clears the queue along with everything else it
already reset.

`tests/test_playback.cc` exercises this end to end: `rewind`/
`fastforward` arm a timer and advance one frame per fired callback,
`pause` clears the pending timer (and a stale `fireTimer()` afterward is
correctly a no-op), and `restart` seeks to frame 0 without itself arming
one.

### Phase 0b (continued): a shared NetCDF fixture builder

`test_varlist.cc`, `test_time_fmt.cc`, `test_dataset.cc`,
`test_controller_characterization.cc`, and now `test_playback.cc` each
hand-rolled their own ~30-line "create a temp file, `nc_def_dim` a few
axes, `nc_def_var` a time coordinate with units, `nc_def_var` the data
variable, `nc_put_var`, `nc_close`" sequence -- only the shapes and
values actually differed between them. `tests/support/nc_fixture.h` adds
`NcFixture`, a chainable RAII builder covering that common shape (`.dim`,
`.timeAxis`, `.coord` for a plain lat/lon-style coordinate, `.var` with a
pluggable data generator, `.missing` for a real `_FillValue` attribute),
deferring the actual `nc_create`/define-mode/data-mode sequence to the
first call to `.path()`/`.openForCore()` so the chain reads
declaratively regardless of what order things are added in.
`tests/test_nc_fixture.cc` proves the files it builds load correctly
(shape, time-axis recognition, generator output, fill values, and that
two fixtures with the same variable name in different files don't
collide); `test_playback.cc` was migrated onto it as the first real
caller. The remaining hand-rolled builders in the other four files are
left as-is for now -- migrating a file that already has passing tests
carries its own small risk for no behavior change, so it's deferred
until one of them needs a genuine change anyway. Pixel goldens
(`tests/support/pgm.h`) are still open.

## Phase 1: dead code, and a dispatch layer that stopped dispatching

`do_buttons.cc`'s 21 `do_*()` functions had become pure two-line
forwarders onto `g_app.controller` (their logic moved there at
OOP_redesign's Step 7); nothing was left in their bodies but the
forward. Deleted, updating every real call site (`ui/src/interface_fltk.cc`'s
`NCVIEW_TEST_DIALOG` hook, `util.cc`'s error-path pause, `view.cc`'s
`stop_on_restart` pause, `test_controller_characterization.cc`) to call
the corresponding `ViewerController` method directly --
`g_app.controller.range(modifier)`, not `do_range(modifier)`.
`do_buttons.cc` now only keeps `which_button_pressed()`,
`in_button_pressed()`, and `in_colormap_selected()` -- the parts with no
direct-call equivalent. Also deleted: `view_forward()`/`view_backward()`
(declared in `protos.h`, never defined anywhere, never called -- dead
upstream declarations) and `redraw_ccontour()` (a one-line wrapper
around `view_draw()` with zero callers).

Per the plan's "tests first" rule, `test_button_dispatch.cc` was written
and landed in its own commit *before* this refactor, driving
`in_button_pressed()` through every `Button` enumerator except `Quit`
(which calls `exit(0)` for real -- no test double for that) across all
four `Modifier` values, run against the unmodified do_buttons.cc. Doing
so surfaced a real, previously-unreachable bug in the test double
itself: `RecordingViewerUi::in_set_scan_dims()` always left
`*new_dim_list` null, but `View::setScanDims()`'s cancel check is
documented dead code (an upstream quirk preserved verbatim: the returned
status is never actually `Message::Cancel`'s numeric value), so it
always falls through to dereferencing the list regardless -- meaning
`Button::Dimset` had never actually been exercised through the stub
before this test tried to. Fixed by having the stub echo back the
current X/Y axes, matching what a real "accept unchanged" dialog answer
would populate.

Verified the refactor changed nothing observable: the same 86 tests,
1226 assertions, all still passing unchanged after the do_buttons.cc
edit, plus the full 4-gate suite, ASan/UBSan/LSan, and a 5-seed
`--order-by=rand` run.

## Phase 2: the last `view.cc` entry points

The 12 remaining free functions in `view.cc` each carried their own
`if (view == NULL) ...` guard: `view_current_nt`, `change_view`,
`view_draw`, `view_change_cur_dim`, `view_set_cur_dim_index`,
`view_get_cur_dim_index`, `invalidate_all_saveframes`,
`view_report_position`, `set_min_from_curdata`, `set_max_from_curdata`,
`plot_XY`, `view_recompute_colorbar`. That guard was never really about
`View` possibly being null in general -- it's a stand-in for a session
fact ("no variable is selected yet") that these functions had no other
way to ask, since they had no session to ask. `ViewerSession`/
`ViewerController` own the active `unique_ptr<ViewState>` and can check
that fact once, internally, so that's where these moved.

`ViewerController` gained a real `ViewerSession &session_` (bound in a
new constructor, `ViewerController(ViewerSession &session)`); `AppContext`
gained a matching constructor, `AppContext() : controller(session) {}`,
since a reference member means `ViewerController` -- and so `AppContext`
-- can no longer be default-constructed or brace-initialized as an
aggregate. Nothing in the tree did either (checked before making the
change), so this cost nothing.

Every move kept the method-conversion pattern this plan has used
throughout: a local alias (`std::unique_ptr<ViewState> &view =
session_.activeView();`, or `view_`/`frame_cache_` directly inside a
`ViewerSession` method) stands in for the old global name, so each
function's body is otherwise byte-identical -- guard text included. The
one deliberate exception: `view_current_nt`/`view_get_cur_dim_index`/
`invalidate_all_saveframes` went onto `ViewerSession` as `currentNt()`/
`curDimIndex()`/`invalidateAllSaveframes()`; the other nine went onto
`ViewerController` as `draw()`, `stepView()`, `changeCurDim()`,
`setCurDimIndex()`, `reportPosition()`, `setMinFromCurdata()`,
`setMaxFromCurdata()`, `plotXY()`, `recomputeColorbar()`. Internal
callers that used to reach these through the free-function name now call
the sibling method directly (`draw(true, false)` from inside another
`ViewerController` method) or through `g_app.controller`/`g_app.session`
from everywhere else (`view.cc`'s own `set_scan_variable`/
`view_change_transform`, `overlay.cc`, `do_print.cc`, `ui/src/main_window.cc`,
`ui/src/interface_fltk.cc`). `protos.h`'s "in view.c" block lost these 12
declarations, keeping only the genuinely free helpers (`set_scan_variable`,
`view_report_position_vals`, `beep`, `view_get_scaled_size`,
`view_change_transform` -- each stays free for its own reason: an
entry-point orchestrator, reads file-static state instead of `view`, a
UI-seam call, or a pure helper).

A postscript, not in the original 12: `view_construct_scalar_coord_str()`
also carried a `view == NULL` guard textually, but both its call sites
(`View::setScanButtons()`, `View::scanToPlace()`) were already `View`
methods, where the global `view` being read is always exactly the `this`
whose method is running -- the guard was never actually reachable there.
Moved onto `View` as a private `constructScalarCoordStr()` instead of
onto `ViewerSession`/`ViewerController`, since it isn't a session-level
question; the dead guard is kept verbatim anyway, matching this
codebase's move-don't-split rule (see `View::setScanDims()`'s similarly
dead cancel check for precedent).

**Tests first, and what running them (not just reading the code) found.**
Four new files -- `test_view_null_guards.cc` (all 12 relocated entry
points invoked with no variable selected, pinning today's exact no-op),
`test_view_navigation.cc` (`change_view` across FRAMES/PERCENT, both
wrap directions, `stop_on_restart`, the `delta==0` expose-event path),
`test_view_draw.cc` (framestore on/off, forced range, autoscale, a
constant-valued degenerate case), `test_view_dims.cc` (dim navigation
round-trip, clamping, X/Y-axis-move rejection) -- were written and
landed in their own commit, verified passing against the *unmodified*
free functions, before anything moved. `test_playback.cc` (Phase 0b)
already covered the playback-adjacent controller methods end to end, so
nothing needed adding there.

Running those tests against the unmodified code (not just reading it)
surfaced two real, non-obvious behaviors neither the plan nor a read of
the source predicted:

- `force_range_to_frame` has **no visible effect** when
  `allow_framestore_usage` is true and the current frame is already
  cached. `options.save_frames` defaults to `true`
  (`DEFAULT_SAVEFRAMES`), so a freshly-selected variable's first frame is
  normally already in the framestore by the time a test calls
  `view_draw()` again -- the framestore-hit path returns *before* the
  force-range recompute block ever runs. `test_view_draw.cc`'s
  force-range test calls `view_draw(false, true)` (disallowing the
  framestore) to actually exercise the recompute, and says why in a
  comment.
- `change_view`'s `stop_on_restart` branch does not reset the frame
  index to 0 when it "wraps" -- it returns immediately, before
  `View::scanToPlace(place)` ever runs, leaving the index exactly where
  it was. That's the actual mechanism that stops the movie (the position
  never advances past the last frame), not a visible snap back to frame
  0 as the name might suggest.

A **third**, genuinely order-dependent bug turned up only under
`--order-by=rand` (seed 4 out of 8 tried): the framestore-off test set
`options.save_frames = false` and then called a helper that, on
whichever test happens to run first in the whole process, triggers the
one-time `initialize_misc()` -- which itself calls
`reset_session_defaults()`, resetting `save_frames` back to `true` and
silently undoing the override. Not a production bug: `SessionFixture`
already resets `options.save_frames` correctly on every test; the issue
was purely in this new test's ordering relative to the one-time init.
Fixed by calling `ensure_ncview_misc_initialized()` explicitly before
setting the override, not after. This is exactly the class of bug the
plan's Phase 0a shuffled-order run exists to catch, and it worked.

Verified the refactor changed nothing observable: the same 106 tests,
1757 assertions, all still passing unchanged after the move, plus the
full 4-gate suite, ASan/UBSan/LSan, and an 8-seed `--order-by=rand` run
(after the ordering fix above).

**Deferred, not blocking:** pixel goldens for `test_view_draw.cc` (the
`tests/support/pgm.h` infrastructure from Phase 0b's sketch still isn't
built); exercising `view_draw()`'s `lockout_view_changes` re-entrancy
guard for real, which needs `RecordingViewerUi`'s stubs to be able to
re-enter core mid-call (a nested `draw()` fired from inside a dialog
callback) -- not something the current stub does, and its own piece of
design work, not a Phase 2 side quest.

## Phase 3: verify, then split (3a/3b so far)

A three-agent codebase inventory preceded this phase and replaced several
of the plan's guesses with facts -- see the plan file's "Reassess here"
section (2026-09-09) for the full list. Two corrections were significant
enough to change what got built:

**3a -- CI had never run on this branch.** `.github/workflows/ci.yml`
triggered on push to `master` and on pull requests only; `OOP_redesign`
had neither. All 29 commits since the branch was created (everything in
this file above this section) had been verified on Linux alone. Added
`OOP_redesign` to `ci.yml`'s push branches and, while there, a "Test
(shuffled order)" step after every job's existing `ctest` step -- run by
invoking the test binary directly with `--order-by=rand`, since `ctest`
has no supported way to forward extra arguments to the test command it
runs. First run: all five jobs (linux, macos, windows, sanitize,
linux-static) passed clean, including the new shuffled step -- the
branch was in good shape, it had simply never been checked.

**3b -- one lead in the plan was wrong; the other was real.** The plan's
"Reassess" section claimed `view.cc:826`'s `netcdf_fi_initialize()` call
(inside `View::checkNewData()`) leaked a file descriptor once per second
during paused playback. Checking the surrounding lines before writing a
test for it found `view.cc:831` is `nc_close(t_ncid)` -- `git log -L`
confirmed this close call predates the entire refactor branch. No leak;
nothing to fix. Recorded as a correction rather than silently dropped,
since the plan's own inventory-then-act discipline is only worth
following if a wrong inventory claim gets caught and written down, not
quietly abandoned.

The second lead, checked independently rather than taken on trust,
turned out to be real: `do_print()`/`build_print_info()` (`do_print.cc`)
dereference `view->variable` and friends roughly 30 times with no null
guard, reachable via `Button::Print`. `test_button_dispatch.cc`'s
dispatch loop always selects a variable before testing `Button::Print`,
so this path had never executed. Confirmed by temporarily adding a
scratch test that dispatched `Button::Print` with no variable selected
(reverted before committing, never landed) -- SIGSEGV, immediately.
Fixed with the same guard pattern Phase 2 used throughout: an early
`if (view == NULL) return;` in `do_print()`, matching the "no variable
selected yet" session fact rather than "view might be null" in general.
`test_view_null_guards.cc` gained a test pinning the fixed (no-op)
behavior.

This is also the first bug in this plan that couldn't be "tests first"
in the usual sense -- the unmodified behavior was a crash, not a wrong
answer, so there's no way to commit a normally-passing test against it.
Handled the same way Phase 0a's and Phase 1's crash-class bugs were:
verify the crash manually (here, with a throwaway test case, run once,
then discarded before committing), fix and the regression test land
together, and the verification section documents that the crash was
reproduced rather than assumed.

Verified: 107 tests / 1759 assertions (up from 106/1757), the full
4-gate suite including all 13 `ui_smoke.sh` goldens byte-identical
(including `print`, confirming the guard doesn't change the with-a-
variable-selected path), a 2-seed `--order-by=rand` check, and a clean
scratch ASan/UBSan/LSan build.

**Next: 3c** -- test the multi-file/file-series read path
(`fi_get_data_iterate`, `virt_to_actual_place`'s multi-file branch,
`fi_dim_value_convert`), the largest untested surface the inventory
found and the feature the program exists for.

## Phase 3c: the multi-file read path

`fi_get_data_iterate()` (`file.cc`), `virt_to_actual_place()`'s
multi-file branch (`util.cc`), `fi_dim_value_convert()`'s cross-file
time-unit reconciliation (`file.cc`), and
`Dataset::cacheScalarCoordInfo()`'s `timestep_2_fdb` construction
(`dataset.cc`) all had zero direct tests -- every existing `fi_get_data`
test used a single-file variable, so `fi_get_data()`'s
`is_virtual && count[0]>1` branch that delegates to
`fi_get_data_iterate()` had never actually run.

`NcFixture` deliberately doesn't cover virtual multi-file variables (its
own header comment says so, by design -- Phase 0b scoped it to the
common single-file shape). `tests/test_multifile.cc` hand-rolls its
fixture the way `test_varlist.cc`'s `make_virtual_piece()` already does,
extended with a distinguishing per-file data offset (so a
boundary-spanning read's returned values can be traced to the file they
actually came from) and, for the reconciliation test, deliberately
different time units on each file's record dimension.

Covers: `fi_get_data`/`fi_get_data_iterate` spanning a file boundary
(reads starting mid-file, ending mid-file, and covering the whole
series); `virt_to_actual_place` at every file's first and last virtual
timestep in a 3-file (3+2+4 timestep) series; `Dataset::
cacheScalarCoordInfo`'s `timestep_2_fdb` mapping every virtual timestep
to its owning `FDBlist*`; and `fi_dim_value` reconciling a value read
from a file whose time units differ from the series' first file (day 5
in "days since 2000-01-15" correctly reconciled to day 19 in "days
since 2000-01-01", a 14-day epoch difference).

**One real bug, in the new test's own fixture, not in production code.**
The first draft declared "time" as a plain fixed-size dimension. netCDF
only treats the *unlimited* dimension as the record dimension, and
`netcdf_fill_aux_data()` only ever populates `FDBlist::recdim_units`
from the record dimension's units attribute -- so with a fixed-size
"time" dim, `recdim_units` stayed empty on every file, which is exactly
the guard condition that makes `fi_dim_value_convert()` skip
reconciliation. The reconciliation test passed for the wrong reason (it
would have passed identically whether or not the conversion code ran at
all) until this was caught by checking *why* it passed, not just that it
did. Fixed by declaring the dimension `NC_UNLIMITED`. That surfaced a
second, related mistake: writing to an unlimited-dimension variable with
`nc_put_var_*` ("whole variable") infers the write shape from the
dimension's *current* length, which for a fresh unlimited dimension is
0 -- it silently writes zero records. `var->size[0]` coming back as 0
caught this immediately (a `REQUIRE` failure, not a silent pass). Fixed
by writing via `nc_put_vara_*` with an explicit start/count instead.

Verified: 113 tests / 2162 assertions (up from 107/1759), full 4-gate
suite including all 13 `ui_smoke.sh` goldens byte-identical, two
`--order-by=rand` seeds, and a clean scratch ASan/UBSan/LSan build.

## Phase 3d: delete the confirmed dead code

No behavior change; every removal individually re-verified for zero
callers before deletion, rather than trusted from the inventory as-is
(see the correction below for why that mattered).

Removed: `file.cc`'s `fi_confirm`/`fi_writable`/`fi_has_dim_values`
(dead wrapper layer -- any real caller already reaches the `netcdf_*`
backend directly); `file_netcdf.cc`'s `netcdf_fi_writable` (orphaned the
moment `fi_writable` goes, since that was its only caller) and
`netcdf_vartype` (already zero callers); `util.cc`'s
`warn_if_file_exits` and `get_group_list`; `stringlist_add_string_ordered`
(its own doc comment already said "Nothing in this codebase calls this
function" -- confirmed and removed rather than left as a documented
oddity); `udu.cc:332-355`'s `#else` stub block
(`udu_utinit`/`udu_utistime`/`udu_calc_tgran`/`udu_fmt_time` no-ops),
unreachable since `core/CMakeLists.txt` unconditionally defines
`HAVE_UDUNITS2`; and four `protos.h` declarations with no definition
anywhere in the tree at all -- `fi_n_dim_entries`, `n_strings_in_list`,
`sl_cat`, `interp`. Also made `view.cc`'s `beep()` and `overlay.cc`'s
`gen_overlay_internal_mapped()`/`overlay_find_closest_pt_inner()`
`static` -- each had external linkage, no header declaration, and no
caller outside its own file.

**A correction to the inventory this phase started from.** It flagged
`util.cc`'s `month_name[12]` as unused. It isn't: `fmt_time()`'s
`TimeStandard::Months` branch reads `month_name[month]`. Caught by
grepping for the symbol before deleting it, not by trusting the earlier
report -- the same discipline that caught Phase 3b's wrong "descriptor
leak" claim. Left alone.

Verified: clean `-Werror` build, the same 113 tests / 2162 assertions
unchanged, `ncview_core_linkcheck` exit 0 (the direct check that none of
these deletions left a dangling reference), all 13 `ui_smoke.sh` goldens
byte-identical, two `--order-by=rand` seeds, and a clean scratch
ASan/UBSan/LSan build.

## Phase 3e: move ViewerController/ViewerSession method bodies out of view.cc

Pure file motion -- no signature or logic change, so (unlike every other
phase) no new tests. Phase 2 moved 12 free functions' *names* onto
`ViewerSession`/`ViewerController`, but their *bodies* stayed in
`view.cc`; `viewer_controller.cc` was 360 lines while its 9 largest
methods lived in a file named `view.cc`. Moved: the 9
`ViewerController::` bodies (`stepView`/`draw`/`changeCurDim`/
`setCurDimIndex`/`reportPosition`/`setMinFromCurdata`/
`setMaxFromCurdata`/`plotXY`/`recomputeColorbar`) to
`viewer_controller.cc`; the 3 `ViewerSession::` bodies (`currentNt`/
`curDimIndex`/`invalidateAllSaveframes`) to `viewer_session.cc`.

Four `view.cc`-local helpers are called from both sides of the new TU
boundary: `lockout_view_changes` (also read by `View::changeDat`, which
stays), `invalidate_variable`, `mouse_xy_to_data_xy`,
`view_data_edit_warn` (each also called by a `View::` method that
stays). New `core/src/view_internal.h` -- private to `core/src`, not in
`protos.h`, not installed -- declares these; each keeps its *definition*
in `view.cc`, next to its other, staying caller. `beep()` needed no such
treatment: its only caller, `stepView()`, moved out entirely, so `beep()`
moved with it into `viewer_controller.cc` instead of being shared.

`view.cc`: 2602 lines (down from ~3349 pre-Phase-2). `viewer_controller.cc`:
1049 (up from 360). `viewer_session.cc`: 158 (up from 78).

Verified: clean `-Werror` rebuild from scratch, both `ctest` targets
(113/113 doctest cases unchanged, all 13 `ui_smoke.sh` goldens
byte-identical), `ncview_core_linkcheck` exit 0, two `--order-by=rand`
seeds, and a clean scratch ASan/UBSan/LSan build.

## Phase 3f: lift ncview.cc's license text into its own file

`useage()`/`print_no_warranty()`/`print_copying()` moved verbatim to a
new `core/src/legal_text.cc`. `print_copying()`'s verbatim GPLv3 text
alone was 630 of `ncview.cc`'s 1615 lines (39%); `useage()`'s help text
and `print_no_warranty()` added another ~50 -- confirmed by checking
what each function actually touched (grepped for anything besides
`fprintf`/`printf`/`exit`; found nothing) rather than assuming from the
inventory. `print_disclaimer()` stays in `ncview.cc`: it reads
`PROGRAM_ID` and is genuinely part of the startup sequence, not static
text. Declarations stay in `protos.h`'s existing "in ncview.c" block --
this phase is the `.cc` split, not a header reorg (that idea is
explicitly deferred; see the plan file).

`ncview.cc`: 1615 -> 894 lines. `legal_text.cc`: 755 lines.

Same verification as 3e, same result: clean, all counts unchanged.

**Phase 3 (3a-3f) is now complete.** Per the plan, everything past this
point (the original Phases 4-9 sketches) needs re-scoping against the
tree as it looks now before starting -- see the plan file's "Later
phases" section.

## Reassessment round 2, and Phase 4a: coverage for overlay.cc and do_print.cc

A second re-scoping pass (2026-09-09, in plan mode) checked what Phase 3
actually touched before trusting the stale Phase 4-9 sketches: it left
`util.cc`, `overlay.cc`, `do_print.cc` and `handle_rc_file.cc` alone
(bar two dead-code deletions in `util.cc`), so the original inventory's
findings about those four files still held, re-verified directly. Chose
"coverage first, then dissolve `util.cc`" as the next fully-specified
phase over the `fi_*`/`netcdf_*` collapse, the `ui/` split, and CI
coverage/fuzzing -- all deferred again. See the plan file's "Reassess
here, round 2" section.

**4a**: `overlay.cc` (673 lines) and `do_print.cc` (273 lines) both had
zero direct tests. Most of `overlay.cc`'s helpers (`gen_xform`,
`gen_overlay_internal`, `gen_overlay_internal_mapped`, `do_overlay_inner`,
`overlay_find_closest_pt`/`_inner`) are `static` -- every test goes
through the public entry points (`do_overlay`/`gen_overlay`,
`overlay_names`/`overlay_current`/`overlay_init`/
`determine_overlay_base_dir`; `do_print`) instead. `build_print_info()`
(`do_print.cc`, also `static`) is likewise only reachable through
`do_print()`.

Extended `RecordingViewerUi` (`tests/stub_interface.cc`) two ways, the
same category of change as Phase 0b's timer queue and the existing
dialog-response globals: `printer_options()` now applies a scriptable
`std::function<void(PrintOptions&)>` (`g_printer_options_override`) to
the `PrintOptions` it's handed, simulating a user editing one field in
the real dialog, instead of discarding the pointer entirely; `in_print()`
now captures the `PrintInfo`/`PrintOptions` it's given
(`g_last_print_info`/`g_last_print_options`) instead of discarding both
arguments, so a test can assert on what `build_print_info()` actually
produced.

**Two real fixture gaps, not production bugs**, both found via SIGSEGV
while writing `test_overlay.cc` and understood before being "fixed" in
the test helper rather than in production code:
- `gen_xform()` reads `NCDim::values` directly. That array is only ever
  populated by `Dataset::calcDimMinmaxes()`, normally run once by
  `ncview.cc`'s `initialize_file_interface()` during real startup.
  Nothing before this test needed `NCDim::values` populated, so no
  existing selection helper called it -- confirmed correct in production
  by tracing the real startup sequence; the test fixture now calls it
  too.
- `do_overlay()`'s redraw path (`data_to_pixels()`, `util.cc`)
  substitutes `fill_value` into every overlay-masked pixel before
  rendering, then indexes `pixel_transform[0]` for it. A real ncview
  always has a colormap installed before the first draw
  (`initialize_colormaps()`); no existing test fixture did, because
  nothing before this test ever triggered a real draw with an active
  overlay mask. The test helper now sets up a minimal identity
  `pixel_transform`, the same way `test_pixels.cc` already does for its
  own direct `FrameRenderer` tests.

`gen_xform()`'s antimeridian/pole test needed real, hand-chosen
coordinate values (lon ascending -180..170, lat descending 90..-90) --
`NcFixture`'s `.coord()` always fills 0,1,2,...,n-1, so this file
hand-rolls its fixture via raw netCDF calls, the same pattern
`test_multifile.cc` (Phase 3c) already established for cases `NcFixture`
doesn't cover. Along the way, working out the expected index for a
"near the pole/antimeridian" test point surfaced a real, worth-pinning
quirk in `gen_overlay()`'s point-placement check
(`if ((i>0) && (j>0)) overlay[...] = 1;`, `overlay.cc`): a point whose
nearest grid cell is index 0 on *either* axis is silently never marked,
even though `gen_xform()` resolved it correctly -- `>` where `>=` would
include it. Also confirmed `gen_xform()` never actually returns the `-2`
sentinel `gen_overlay()`/`gen_overlay_internal()` check for (`if (i==-2)
return {};`) -- it only ever returns `-1` (out of range) or a valid
index, making that check dead code from a since-changed version. Neither
is fixed here: this phase is coverage, not a behavior change, and both
are now pinned by a test that would fail if either changed silently.

The 2-D-mapped-coordinate branch (`gen_overlay_internal_mapped()`, the
only caller of `overlay_find_closest_pt()`) is **not** covered: it needs
a variable with curvilinear (2-D lat/lon) coordinates, which `NcFixture`
doesn't build and nothing else in the tree does either. Building that is
its own piece of work (useful for curvilinear-grid coverage generally,
not just this one function), not a Phase 4a side quest.

Test count: 113 -> 131 tests, 2162 -> 3841 assertions. Full verification
(4-gate + ASan/UBSan/LSan + 2-seed shuffle) clean.

**Next: 4b** -- dissolve `util.cc` into the four modules the inventory
already identified (render pipeline, `Dataset`-adjacent metadata setup,
time formatting, string helpers), now that the two previously-zero-
coverage files it's adjacent to (`overlay.cc`, `do_print.cc`) have real
tests.

## Phase 4b: dissolve `util.cc`

`util.cc` (1,688 lines after Phase 3d's dead-code deletions) is gone.
Its contents moved into five new files, in four tested-then-moved groups
plus a final cleanup commit for the three leftover functions that didn't
fit any group:

- **`render_pipeline.cc`** (group 1): `data_to_pixels()`/`expand_data()`
  already took a `View*` as their sole argument, so they became
  `View::dataToPixels()` and private `View::expandData()` -- a straight
  "move, don't split" method conversion, with the three call sites
  (`view.cc` x2, `viewer_controller.cc`) updated to `view->dataToPixels()`.
  `contract_data()` (already `View`-shaped, but file-`static`) became
  private `View::contractData()` the same way. `close_enough`/`clip_f`/
  `util_mean`/`util_mode`/`data_has_mv` have no natural `View` to attach
  to and stayed free functions, moved verbatim. `tests/test_shrink.cc`
  and `tests/test_expand.cc` (16 new cases) landed first, against the
  unmodified free functions, and cover `ShrinkMethod::Mean` vs `Mode`,
  `util_mode()`'s first-encountered tie-break, a shrink window containing
  a missing value, a non-integer shrink factor's edge-clamp, `Replicate`
  vs `Bilinear` at several blowup factors, and `Bilinear`'s corner/edge
  handling.
- **`var_metadata.cc`** (group 2): `virt_to_actual_place`,
  `handle_dim_mapping`/`_scalar`/`_2d`, `fill_dim_structs`,
  `is_scannable`, `determine_lat_lon`. **Correction to this plan's own
  inventory**, found by reading `dataset.h`'s own header comment before
  moving anything (per this plan's "verify before acting" discipline):
  it records a *prior, deliberate* design decision that these functions
  "stay free functions ... called from Dataset's methods the same way
  anything else calls them", specifically because they only fill in an
  already-allocated `NCVar*`'s fields and never touch `Dataset`'s
  variable list itself -- unlike `add_var_to_list`/`cache_scalar_coord_
  info`/etc., which *did* become `Dataset` methods in an earlier phase.
  This plan's Phase 4 sketch called for "private Dataset methods" here;
  that was wrong, and this phase respects the existing decision instead
  of re-litigating it. `tests/test_dim_mapping.cc` (7 new cases) covers
  the previously-untested 2-D curvilinear "coordinates" attribute mapping
  (WRF-style `XLAT`/`XLONG`, plain `lat`/`lon`, bare `Y`/`X`, and an
  unrecognized name that abandons the mapping). **Second correction**,
  also found while writing that file: `determine_lat_lon()` classifies a
  coordinate variable's *name* (a `lat`/`lon` prefix or substring,
  case-insensitive, falling back to a bare `x`/`y` first letter) -- not a
  *units string*, as the plan described.
- **`epic_time.cc`** (group 3, merged into the existing file rather than
  a new one): `handle_time_dim`/`months_calc_tgran`/`fmt_time`, the
  `TimeStandard` dispatch layer shared across this file's `Epic0`
  functions and `udu.cc`'s `Udunits` ones. Landed in the same commit as
  group 2 rather than after its own separate tests-first step: group 2's
  `fill_dim_structs()` calls `handle_time_dim()`, which was file-`static`
  in `util.cc` -- splitting the two groups into separate TUs without
  moving both at once would have left a real (if temporary) link error,
  so they moved together. `handle_time_dim()` gained external linkage
  (declared in `protos.h`) for exactly that reason; `months_calc_tgran()`
  stayed a private helper. Existing coverage (`tests/test_time_fmt.cc`,
  9 cases across every `TimeStandard`/calendar combination) already
  exercised these paths and needed no expansion to stay green through
  the move.
- **`varname_utils.cc`** (group 4, moved first as the simplest, purely
  mechanical group): `limit_string`, `strncmp_nocase`, `count_nslashes`,
  `unpack_groupname`, `varname_no_groups`, `n_vars_in_list`. Pure file
  motion, already covered by `tests/test_util.cc`/`test_varlist.cc`, no
  new tests needed.
- **Final cleanup commit**: the three functions left in `util.cc` after
  the four groups moved out didn't fit any of them. `new_netcdf()` had
  exactly one caller (`Dataset`'s `new_fdblist()`) and moved into
  `dataset.cc`'s existing anonymous namespace, losing its external
  linkage entirely. `set_blowup_type()` moved to `viewer_controller.cc`
  (its primary caller, `ViewerController::blowupType()`) -- it stays a
  free function; nothing owns it uniquely enough to justify a method.
  `in_error()` moved to `viewer_ui_bridge.cc`, alongside every other
  UI-seam forwarder, even though it forwards to `in_dialog()` (another
  free function) rather than directly to `g_app.ui`. With all three
  moved, `util.cc` was empty and deleted. Two stale doc comments this
  move left behind got fixed in the same commit: `protos.h`'s "`in_error`
  lives in `util.cc`" and `dataset.h`'s "stay free functions in
  `util.cc`".

`set_blowup_type`'s destination (flagged in the plan as needing a
decision, since the inventory didn't assign one) is `viewer_controller.cc`,
decided by checking its actual callers directly: `ViewerController::
blowupType()` and one call inside `view.cc`'s `set_scan_variable()`
(itself not yet a `View`/`ViewerController` method) -- majority caller
wins, consistent with `set_blowup_type` staying a free function rather
than becoming a method of either.

Test count: 131 -> 147 tests, 3841 -> 3982 assertions (23 new
characterization cases across `test_shrink.cc`/`test_expand.cc`/
`test_dim_mapping.cc`; every other group was pure motion with no new
tests needed). Full verification (4-gate + ASan/UBSan/LSan + shuffled
order at seeds 1, 7, 42, 99) clean at every commit along the way, not
just at the end.

## Reassessment round 3, and Phase 5a: characterizing the file-I/O layer

A three-way parallel survey of `file.cc`/`file_netcdf.cc`, the three
remaining "small module-state files" (`overlay.cc`, `handle_rc_file.cc`,
`do_print.cc`), and `ncview.cc`/`ui/`/CI, run against the stale Phase
5/6/8 sketches written before Phase 4b existed. **All three sketches were
materially wrong**, corrected directly from the current code rather than
patched from memory:

- The `fi_*`/`netcdf_*` bypass is **14 call sites, not 5** (`view.cc`,
  `dataset.cc`, `var_metadata.cc`, `epic_time.cc`), and the layering is
  **circular** -- `file_netcdf.cc` calls back *up* into `file.cc`'s
  `fi_scannable_dims()` (`file_netcdf.cc:200`) and `fi_n_dims()` (`:455`,
  `:552`). Every `fi_*()` forwarder dispatches on a `file_type` static
  that only ever holds `FILE_TYPE_NETCDF`, with `fprintf`+`exit(-1)` as
  the only alternative branch -- a single-backend layer with a dead
  switch, not an abstraction over two backends. None of the 16 `fi_*()`
  entry points had a single direct test; every existing test reaches past
  them to `netcdf_*()`.
- `handle_rc_file.cc` has **one** `Stringlist**` out-parameter, not a
  pair, and **zero** file-scope state -- the old Phase 5 sketch's premise
  ("module state, becomes a `PersistentState` class") didn't survive
  contact with the actual file. Its real defects are two `exit(-1)` calls
  inside a library function and an untested error ladder.
- `do_print.cc`'s `static PrintOptions printopts` is the one genuinely
  ownerless module-static of the three (`overlay.cc`'s `my_current_overlay`
  has a getter; `handle_rc_file.cc` has none at all) -- the sketch had
  called `do_print.cc` "lowest value; do last or skip."
- Three of Phase 8's four premises about `ui/` were also wrong:
  `DimValueSlider` is a 16-line anonymous-namespace class, not a peer of
  `MainWindow` (which is ~1,309 of `main_window.cc`'s 1,725 lines alone);
  `plot_window.cc` holds two classes, so it isn't the one-class-per-file
  model the sketch pointed at; and `interface_fltk.cc`'s `Button`-enum
  test-hook table is already done -- the remaining `strcmp` chain is a
  different hook (`NCVIEW_TEST_DIALOG`).

Chosen for this round: characterize the `fi_*` layer (5a, this section),
then give `do_print.cc`'s `printopts` an owner (5b) and dedupe
`Colorbar::draw()`'s copy of `FrameRenderer`'s transform formula (5c) --
both still pending. The `fi_*`/`netcdf_*` collapse (Phase 6), `ncview.cc`'s
zero-coverage `parse_options()`/colormap code (Phase 7), and the `ui/`
split (Phase 8) are all deferred again, re-derived rather than resumed
from their stale sketches. Full detail in the plan file's "Reassess here,
round 3" section.

`tests/test_file_layer.cc` (Phase 5a) pins the `fi_*()` dispatch layer
before Phase 6 gets to delete any of it: the 13 pure forwarders against
their `netcdf_*()` counterparts, `fi_dim_calendar`'s command-line-override
branch, `fi_initialize`'s open+`addVariables` path, `determine_file_type`'s
accept path (the reject path `exit(-1)`s, so it can't be tested
in-process -- a deliberate, documented gap), `fi_recdim_id`'s missing
`file_type` guard (pinned as-is, not "fixed"), and the circular call-back
in both directions.

Two things this pass confirmed by reading the code rather than assuming:

- `Dataset::addVariable()`'s `nfiles` parameter is threaded all the way
  through from `fi_initialize()` but **never read** in the function body
  (`dataset.cc`). The plan had asked to "assert the `nfiles` argument's
  effect" -- there isn't one to assert, so the new test pins that two
  otherwise-identical opens with different `nfiles` values produce an
  identical `NCVar`, rather than testing for a difference that doesn't
  exist. Left as-is (5a is tests-only); worth a comment for whoever
  eventually touches `fi_initialize()`'s signature.
- `netcdf_fill_aux_data()`/`fi_fill_aux_data()` unconditionally dereference
  `fdb->aux_data.get()` once the target variable has any attributes, with
  **no null check**. This is only safe in production because
  `new_fdblist()` (`dataset.cc`, the sole real caller) always pre-allocates
  `aux_data` first. Confirmed by reproducing the SIGSEGV directly: the
  first draft of the new forwarder-equivalence test built a bare,
  default-constructed `FDBlist` (leaving `aux_data` null) and crashed the
  test binary the moment it called either function against a variable
  with `units`/`long_name` attributes. Fixed in the test fixture (pre-
  allocate `aux_data`, matching `new_fdblist()`), **not** in production
  code -- 5a characterizes, it doesn't refactor. Flagged here as a latent
  defect (unreachable today, since every real caller pre-allocates) worth
  a defensive check whenever Phase 6 touches this function, rather than a
  live bug needing an immediate fix.

147 -> 167 tests, 3982 -> 4484 assertions (20 new characterization cases).
Full verification (4-gate + ASan/UBSan/LSan + shuffled order at seeds 1
and 42) clean.

## Phase 5b: give `do_print.cc`'s `printopts` an owner

`static PrintOptions printopts;` (`do_print.cc`) was the one genuinely
ownerless module-static the round-3 survey found -- no getter, no setter,
reachable only by calling `print_init()` first, which is why
`test_button_dispatch.cc`, `test_do_print.cc` and `test_view_null_guards.cc`
all have to call it by hand before printing can be exercised at all.

Moved onto `ViewerSession` as a `print_settings_` member with a
`printSettings()` accessor (`viewer_session.h`), following the exact
pattern already established there for `RenderSettings`/`PlaybackSettings`/
`SessionDisplayPrefs`/`StartupSettings` -- a `Printer`-shaped wrapper type
was considered and rejected, since `ViewerSession` already is the place
session-scoped settings structs live and a lone struct member needs
nothing a wrapper class would add. `do_print.cc`'s three functions
(`print_init`, `do_print`, `build_print_info`) each bind a local
`PrintOptions &printopts = g_app.session.printSettings();` as their first
statement -- the "move, don't split" pattern applied to a module-static
instead of a free function, so every reference below it in each function
body is untouched. `print_init()`'s call site (`ncview.cc:153`) and every
test call site are unchanged, since the function's own signature and
behavior didn't move, only its storage's owner did.

No behavior change: `printopts`, static or as a `ViewerSession` member,
has the same static storage duration and is zero-initialized either way
before `print_init()` runs at startup, and nothing reads it before that
point in either version. 167 tests / 4484 assertions, unchanged. Full
verification (4-gate + ASan/UBSan/LSan + shuffled order at seeds 1 and
42) clean; `grep -rn` confirms no reference to the old file-scope static
survives outside an explanatory comment.

## Phase 5c: dedupe the colorbar's copy of the transform formula

`ui/src/main_window.cc`'s `Colorbar::draw()` hand-copied
`FrameRenderer::render()`'s transform/invert/scale arithmetic
(`core/src/frame_renderer.cc`). Two problems: it hardcoded `10` where
core uses `settings.n_extra_colors` (both are 10 today, so this was
latent, not a live bug), and its explanatory comment pointed at
`util.cc:data_to_pixels`, a file Phase 4b had already deleted.

Extracted the shared per-sample step into `FrameRenderer::colorIndex<T>`
(`core/include/ncview/frame_renderer.h`): given a normalized value, a
`Transform`, `invert_colors`, `n_colors` and `n_extra_colors`, returns the
color index. **It's a template, not a plain function, and that wasn't
the first thing tried.** The first attempt shared one non-templated
function (`double` parameter, `float` internal storage to keep
`render()`'s arithmetic byte-for-byte identical to its pre-extraction
form) and called it from both sites. That broke 11 of `ui_smoke.sh`'s 13
goldens: `Colorbar::draw()` had always computed in pure `double` with no
intermediate narrowing, and forcing it onto `render()`'s float precision
shifted one pixel column's color index by 1 at a transform/invert
truncation boundary (`Transform::Low`, `invert_colors=true`,
`normalized≈0.9` was the one `tests/test_frame_renderer.cc` caught before
`ui_smoke.sh` confirmed the visual effect — diffed with ImageMagick down
to a single 1×21 differing column in the "initial" golden). The plan
explicitly ruled out introducing behavior changes beyond the
`n_extra_colors` fix, and a precision change is one, however small.
Templating `colorIndex<T>` (`render()` instantiates `T=float`,
`Colorbar::draw()` instantiates `T=double`) keeps one function body as
the actual source of truth while letting each call site keep its
original arithmetic type — genuine deduplication without a behavior
change on either side.

`Colorbar::draw()` now calls `FrameRenderer::colorIndex<double>(...)`
with the real `options.n_extra_colors` instead of the hardcoded `10`
(a no-op today, live if `-ne` is ever wired up), and its stale comment
now points at `frame_renderer.cc`. A second, unrelated stale `util.cc`
reference was found and fixed in the same file while here
(`main_window.cc`'s anonymous-namespace `lookup()` helper's comment).

Two new `tests/test_frame_renderer.cc` cases: one confirms `render()`
actually delegates to `colorIndex<float>` (not a re-drifted inline copy)
by comparing its output against a direct call for every `Transform` ×
`invert_colors` combination; the other reproduces `Colorbar::draw()`'s
pre-extraction double-precision formula independently and checks
`colorIndex<double>` agrees at every input — this second test is the one
that would catch the float/double regression above if it ever came back.
167→169 tests, 4484→4596 assertions. Full verification (4-gate +
ASan/UBSan/LSan + shuffled order at seeds 1 and 42) clean, including all
13 `ui_smoke.sh` goldens byte-identical; `grep -rn` confirms no duplicate
of the arithmetic and no remaining reference to the deleted `util.cc`
survives.

This completes Phase 5 (5a/5b/5c) as scoped in the round-3 reassessment.
Phase 6 (the `fi_*`/`netcdf_*` collapse) is unblocked.

## Phase 6: collapse the `fi_*`/`netcdf_*` double layer (partial, then completed below)

The largest remaining structural phase (~2,780 lines across `file.cc` and
`file_netcdf.cc`), landed as a coherent, fully-verified **subset** of the
plan's Phase 6 scope rather than the whole thing in one pass — the
call-site count for the remaining piece (below) made that the safer
split. Eight commits, each independently verified:

**Dead code and the circular dependency (step 1).** `nc_print_group_structure()`
had zero callers anywhere — confirmed by grep, not assumed — and was
deleted outright. `netcdf_varindex_to_name()`, `netcdf_global_att_string()`
and `netcdf_dimvar_bounds_id()` were declared in `protos.h` with what the
round-3 survey called "zero callers project-wide", but each has a real
caller from *within* `file_netcdf.cc` itself — not dead, just wearing
external linkage they don't need. Made `static`, matching Phase 3d's
precedent for the identical situation. The genuine circular dependency
the round-3 survey found — `file_netcdf.cc` calling back UP into
`file.cc`'s dispatch layer (`fi_scannable_dims()`, `fi_n_dims()` ×2) purely
to reach `netcdf_scannable_dims()`/`netcdf_fi_n_dims()`, the same backend
this file already is — is broken: every call site in `file_netcdf.cc`
now calls the `netcdf_*` primitive directly, like every other call in the
file already did.

**`NetCDFFile::open()` (step 2).** `NetCDFFile` previously had only
`explicit NetCDFFile(int fileid)`, adopting an already-open id; the only
production way to open a file is `fi_initialize()`/`netcdf_fi_initialize()`,
and both `exit(-1)` on failure, so the nonexistent/unreadable/not-netCDF
cases have never been reachable in-process. `NetCDFFile::open(path,
nc_errcode)` opens read-only via `nc_open()` and returns
`std::optional<NetCDFFile>` — `nullopt` on failure, with the netCDF error
code available via the out-parameter. `std::optional`, not
`std::expected`: the project targets C++17. Deliberately **not** wired
into the production startup path — changing `fi_initialize()`'s
`exit()`-on-failure behavior is a separate decision this phase didn't
make. `tests/test_netcdf_file.cc` (new): the RAII guarantee nothing
previously proved (destruction actually closes the fd), reopening the
same path twice, move construction/assignment (including that
move-assignment closes the target's previous file first), and opening N
files then confirming all N close on scope exit.

**A real bug, found by the shuffled-order gate (its own commit).** With
`open()` able to construct a `NetCDFFile` without `determine_file_type()`
ever having run, `NetCDFFile::close()` → `fi_close()` → `file.cc`'s
`file_type` dispatch hit the dead `else` branch and called `exit(-1)`,
killing the whole test binary — reproduced with `--rand-seed=1`.
Previously invisible because the only production path to a `NetCDFFile`
(`Dataset::trackFile()` ← `fi_initialize()`) always ran after
`determine_file_type()`; `open()` doesn't share that invariant and was
never asked to. Fixed by having `NetCDFFile::close()` call
`netcdf_fi_close()` directly — itself a small piece of the collapse,
since `NetCDFFile` is already irrevocably the netCDF backend.

**Removed `Dataset::addVariable()`'s dead `nfiles` parameter.** Phase 5a
had pinned, rather than fixed, that this parameter — threaded from
`ncview.cc` through `fi_initialize()` through `Dataset::addVariables()`
into `Dataset::addVariable()` — was accepted but never read. Now that
this exact call chain is being restructured, removed it outright rather
than carrying it forward: dropped from all three functions, `ncview.cc`'s
now-unused local variable removed, the dedicated characterization test
for its no-op-ness deleted (no longer applicable), and 32 call sites
across 14 test files updated.

**Guarded `netcdf_fill_aux_data()` against a null `aux_data`.** The other
Phase 5a finding: an unconditional `fdb->aux_data.get()` dereference,
safe today only because the sole real caller (`new_fdblist()`) always
pre-allocates it. Added a guard right before the first dereference (the
independent `recdim_units` work above it still runs regardless); a
collapse touching this function is exactly the point at which leaving a
known latent crash in place stops being the safer choice. New regression
test constructs a bare `FDBlist` with no `aux_data` and confirms no crash.

**Moved the multi-file functions onto `Dataset`.** `fi_get_data()`/
`fi_get_data_iterate()`, `fi_dim_value()`, `fi_dim_value_convert()`, and
`fi_fill_value()` all act on an `NCVar` spanning however many files it
actually lives in, not on one open file id — every call site already
passed an `NCVar*` as the primary argument, unlike the 13 single-file
forwarders (see "Deferred" below). Became `Dataset::getData()`/
`getDataIterate()` (private), `Dataset::dimValue()`, and
`Dataset::fillValue()`; `dimValueConvert()` stayed a free function in
`dataset.cc`'s existing anonymous namespace (Phase 4b's `new_netcdf()`/
`new_fdblist()`/`equivalent_FDBs()` precedent) since it touches no
`Dataset` state. Each dropped the same dead `file_type` dispatch step 1
already removed from `NetCDFFile::close()`, calling `netcdf_fi_get_data()`/
`netcdf_dim_value()`/`netcdf_fill_value()` directly. `Dataset::dimValue()`
also drops an `if(1==0){...}` block of unreachable debug `printf()`s
carried along verbatim inside the old `fi_dim_value()` — provably dead,
not a behavior change. 25 call sites across 8 files updated to
`g_dataset.<method>(...)`.

**New coverage for `netcdf_fill_value()`/`netcdf_fill_aux_data()`.**
`_FillValue`/`missing_value` precedence and `scale_factor`/`add_offset`
unpacking had zero coverage anywhere — confirmed by grep. 13 new
`tests/test_file_netcdf.cc` cases cover the three-attribute precedence
order (each found attribute overwrites the last, so a global
`missing_value` beats both var-level attributes — genuinely surprising
until the code is read), the netCDF-type-default fallback, scale/offset
unpacking (alone and combined, and skipped entirely with a null
`aux_data`), and `netcdf_fill_aux_data()`'s own attribute reads. One of
these tests' own first draft got a real, pre-existing behavior wrong and
had to be corrected by reading the code (this plan's standing rule, not
a one-off): with `add_offset` **and** `scale_factor` both set but no
`valid_range` attribute, the "assume they apply to the valid range too"
special case also transforms `valid_min`/`valid_max` in place — not a
bug, but easy to assume otherwise, and now pinned rather than silently
assumed away.

**Deferred at the time, and why** (both resolved below). The 13 pure
single-file forwarders (`fi_list_vars`, `fi_title`, `fi_long_var_name`,
`fi_var_units`, `fi_dim_units`, `fi_n_dims`, `fi_scannable_dims`,
`fi_var_size`, `fi_dim_id_to_name`, `fi_dim_name_to_id`, `fi_dim_longname`,
`fi_recdim_id`, `fi_fill_aux_data`) were *not* migrated onto `NetCDFFile`
methods this round. Unlike the multi-file functions, most call sites hold
only a bare `int fileid` threaded down through several layers of their
own callers (`view.cc`, `viewer_controller.cc`, `viewer_session.cc`,
`do_print.cc`, `var_metadata.cc` — 40+ call sites total, confirmed by grep
before scoping this phase), not an `NCVar*`/`FDBlist*`/`NetCDFFile*`
already in hand. Also deferred: `test_file_metadata.cc` for the ~350
lines of netCDF-4 group-handling code (`file_netcdf.cc`) still at zero
coverage.

169→190 tests, 4596→4831 assertions across this half of the phase. Full
verification (4-gate + ASan/UBSan/LSan + shuffled order across many
seeds) clean at every commit; `grep -rn` confirms no reference survives
to any renamed/moved/deleted symbol, and the circular dependency is
actually broken, not relocated.

## Phase 6, continued: the 13 forwarders, and group-handling coverage

Both items deferred above turned out smaller than the deferral reasoning
assumed, once actually read rather than estimated from a grep count.

**The 13 forwarders, migrated onto `NetCDFFile` methods.** The premise
for deferring this — "most call sites hold only a bare `int fileid`...
not an object already in hand" — was checked directly against every one
of the 40+ call sites (`grep -rn` for each of the 13 function names
across `core/` and `ui/`) and turned out backwards: **every single call
site** already computed its `fileid` via
`some_fdblist_or_ncvar->files.front().get()->id()` (or the equivalent for
a specific file index), immediately before handing that bare `int` to the
dispatcher. None of them held a fileid with no object behind it — they
all held the `FDBlist`, and `FDBlist::file` (a public `NetCDFFile*`
member, already there for `id()` itself to read) was the direct
replacement with no lookup table, no new `Dataset` accessor, and no
signature change to any of the functions that used to compute the bare
`fileid` locally. `view.cc` (largest by far — 8 different methods),
`do_print.cc`, `var_metadata.cc`, `dataset.cc`'s `addVariable()`,
`viewer_session.cc`, and `viewer_controller.cc` (×2) all changed the same
way: drop the `->id()` extraction, call the method on `->file` directly.
A few of `view.cc`'s methods repeated the same `->files.front().get()->id()`
call four or five times in a row for different forwarders — those got one
local `NetCDFFile *file0 = ...` instead of one per call, which is a
readability improvement riding on top of the move but not a behavior
change (each call site still resolves the exact same object it did
before).

Each method body is the `netcdf_*()` call the old forwarder made,
unchanged — `NetCDFFile::listVars()`/`title()`/`longVarName()`/
`varUnits()`/`dimUnits()`/`nDims()`/`scannableDims()`/`varSize()`/
`dimIdToName()`/`dimNameToId()`/`dimLongname()`/`recdimId()`/
`fillAuxData()`, all declared in `dataset.h` alongside the class's
existing `open()`. `fi_initialize()` (the one production caller of
`fi_list_vars()`) now calls `g_dataset.trackFile(id)` to get the
`NetCDFFile*` *before* listing variables (`trackFile()` is idempotent by
fileid, so `addVariables()`'s own later `trackFile()` calls for the same
id just return the same object) — the one place production code needed a
few lines rearranged rather than a pure call-site swap.

`file.cc` drops from 351 to 161 lines, keeping only `fi_initialize()`,
`fi_dim_calendar()` (adds a real command-line-override check beyond
dispatch), `fi_close()`, and `determine_file_type()` — none of which are
pure forwarders, so none collapse further.

`tests/test_file_layer.cc` (Phase 5a's characterization suite, the whole
reason this collapse was provably safe) needed a genuine, deliberate
call-surface update: it used to call the 13 free functions by name, which
no longer exist, so it now calls the `NetCDFFile` methods directly —
still asserting the exact same equivalence against each `netcdf_*()`
counterpart, and still pinning the one real behavioral quirk found in
Phase 5a (`fi_recdim_id()`/now `recdimId()` has no `file_type` guard at
all, unlike its 12 siblings — the collapse must not have added one as an
incidental side effect, and it didn't). `BareFile`, the fixture's test
helper, now owns a `NetCDFFile` member that closes itself on destruction
instead of calling `fi_close()` by hand — the same real close path
production code uses. A second, smaller stale-comment fix landed
alongside: `test_file_netcdf.cc`'s header comment claimed
`netcdf_dim_name_to_id()`/`netcdf_dim_id_to_name()` still routed through
the dispatching `fi_n_dims()` — they don't, since this phase's earlier
circular-dependency break — corrected to describe what's actually true
now rather than repeat a claim already falsified by this phase's own
step 1.

190 tests / 4831 assertions, unchanged — pure motion, no behavior change.

**`test_file_metadata.cc`: coverage for the netCDF-4 group-handling
code.** `NcFixture` documents group support as out of scope, so this
fixture is hand-rolled with raw `nc_*()` calls (`nc_def_grp()` for a
one-level and a two-level-nested group), the same pattern
`test_file_layer.cc`/`test_multifile.cc` already use for shapes
`NcFixture` doesn't cover. Nine new cases: a variable one level and one
two levels deep both show up on `netcdf_fi_list_vars()`'s displayable
list (proving the recursive group walk actually recurses); an explicit
`"grp1/g1_var"`/`"grp1/grp2/g2_var"` path resolves correctly for both
metadata (`netcdf_fi_n_dims()`/`netcdf_fi_var_size()`/
`netcdf_scannable_dims()`) and real data, including a variable in a
child group referencing its *parent's* dimension by id rather than
redeclaring it (confirmed via the plain netCDF-C group API directly,
independent of ncview's own resolution); the three attribute-precedence
edge cases `netcdf_get_char_att()`'s own comment documents but nothing
tested before now — an attribute missing entirely, present but a
zero-length string, and present but stored as the wrong netCDF type
(`NC_INT` where `NC_CHAR` was expected) — all three collapse to the same
empty-string result, now pinned instead of assumed; an unlimited (record)
dimension variable read through the group-aware call path; and a
char-typed (`NC_CHAR`) text variable's storage dimension, which surfaced
a real, previously-invisible quirk worth pinning rather than "fixing":
reading `file_netcdf.cc` directly shows neither
`netcdf_fi_list_vars_inner()` nor `netcdf_scannable_dims()` excludes by
`nc_type` at all — displayability and scannability are decided purely by
dimension count and size, so a sufficiently large text field is listed
and scanned exactly like a numeric variable. Not a bug this pass fixed;
a fact this pass made provable instead of merely suspected.

190→199 tests, 4831→5080 assertions.

**Phase 6 is now fully complete.** Both halves — the collapse itself and
the coverage gap it depended on — landed with full verification (4-gate +
ASan/UBSan/LSan + shuffled order across multiple seeds) clean at every
commit, and `grep -rn` confirms zero surviving references to any of the
13 deleted free functions outside of explanatory comments and unrelated
error-message text that was never a symbol reference to begin with.
169→199 tests, 4596→5080 assertions across the whole phase (both halves
combined).

## Reassess here, round 4: what Phase 6 actually left behind

Two parallel surveys verified Phase 6's actual end state independently
(re-reading the code, not trusting its own completion report) and
re-checked `view.cc`, which Phase 3e/3f explicitly deferred splitting
further pending its own re-argument later. Full detail lives in the plan
file's "Reassess here, round 4" section; summary:

- Phase 6 is confirmed genuinely done: `file.cc` is 161 lines (4
  functions), the 13 migrated forwarders are one-line `NetCDFFile`
  methods, and the `file_netcdf.cc`<->`file.cc` circular dependency is
  confirmed gone (zero remaining `fi_*`/`NetCDFFile` references in
  `file_netcdf.cc`). What's left uncollapsed: 17 direct `netcdf_*`
  bypasses outside `dataset.cc`, and `epic_time.cc`, which was never
  migrated at all -- both flagged as Phase 7b, not fixed here.
- `view.cc` (2,602 lines, largest file in the tree) has real but bounded
  coupling between its five clusters -- not a god-object needing urgent
  breakup, so a structural split remains plausible later but wasn't
  forced. What the survey actually found instead: `View::checkNewData`
  (189 lines, file-growth polling) was promised a test in Phase 3b's own
  writeup and it was never written once the leak lead 3b was chasing
  turned out false; `plotXYSc` (256 lines, the largest `View::` method)
  and the UI-label cluster (~300 lines) were also at zero coverage.

Chosen: test the neglected `view.cc` clusters first (7a), then close
Phase 6's small residue (7b).

## Phase 7a: characterize `view.cc`'s neglected clusters

Three new test files, all landing against completely unmodified code
(this is new coverage, not a refactor -- there's no "before" behavior
to preserve, just real behavior being pinned for the first time).

**`tests/test_view_check_new_data.cc`** -- `View::checkNewData()`
(file-growth polling). Hand-rolled a growable fixture (`NC_UNLIMITED`
time dim, like `test_multifile.cc`'s own reasoning for the same thing)
since `NcFixture` only ever writes fixed-size dims. Covers growth
detected (extends `size[0]`/`var_size[0]`/`timestep_2_fdb`, advances the
current frame by exactly the new timestep count, sets an informative
title label), no-growth (re-arms the 1-second timer via 0b's fake timer
queue), and the `new_frame_times`/`new_frame_nframes` history statics
across repeated growth calls. One fixture-only correction along the way:
`Dataset::cacheScalarCoordInfo()` must be called explicitly before
selecting a variable in a test (production does this once, at startup,
in `ncview.cc:792`) -- without it, `timestep_2_fdb` starts empty rather
than pre-sized to the initial timestep count, which the first draft of
this test's assertions silently mis-indexed into.

**`tests/test_view_xy_plot.cc`** -- `View::setXYPlotAxis()`/`plotXYSc()`
(256 lines, the largest `View::` method), driven through their real UI
entry point, `ViewerController::plotXY()`, rather than calling
`plotXYSc()` directly by hand. Extended `RecordingViewerUi`'s
`in_query_pointer_position()` with a scriptable `g_query_pointer_x/y`
pair (previously hardcoded to (0,0)) and `in_popup_XY_graph()` with a
capture of its arguments (`g_last_xy_n/dimindex/xvals/yvals`) -- both
mirror patterns Phase 4a already established (`g_printer_options_override`,
`g_last_print_info`), and the capture is the *only* way a test can
observe `plot_XY_xvals`/`yvals`/`dim[]`, which are file-static (internal
linkage) inside `view.cc`.

Two real findings, both pinned as-is (this is a coverage phase, not a
behavior-change one):
- `plot_XY_axis` already defaults to the scan axis right after variable
  selection (`determineScanAxes()`, `view.cc:726-729`) -- `plotXY()`'s
  "Error! I have no valid axis to plot along!" branch reads like a real
  precondition a caller must satisfy first, but isn't reachable through
  normal selection at all.
- `plotXY()`'s edge-of-image clamp runs *before* the screen-to-data Y
  inversion (`view.cc:968-969` then `:976-977`), so an off-the-bottom
  click clamps to data row 0, not the last row, under the default
  (non-inverted) orientation -- clamping to size-1 and then inverting
  that lands back on 0.

**`tests/test_view_labels.cc`** -- `View::redrawDimensionInfo()`/
`showCurrentDimValues()`/`labelDimensions()` and
`View::constructScalarCoordStr()` (a 0-D CF scalar coordinate, e.g.
WRF's `XTIME`). Extended `RecordingViewerUi`'s
`in_indicate_active_dim()`/`in_set_cur_dim_value()` to record their
arguments instead of just the bare call name (matching `in_set_label`'s
existing pattern). `constructScalarCoordStr()` is private; reached only
through its one caller, `View::scanToPlace()`, itself only reachable
through the public `stepView()` entry point.

Real finding: `constructScalarCoordStr()`'s "displaying_along_time_dim"
range-format branch (`view.cc:1863`, fires when dimension index 0 is
used as an X or Y image axis) can never be reached under default axis
assignment. `scanToPlace()` itself no-ops whenever `scan_axis_id==-1`
(`view.cc:436-437`), and a variable only lacks a scan axis when its
last two dims consume the whole variable (exactly the 2-dim case where
dim 0 *would* be an image axis) -- so the one condition that would make
dim 0 an X/Y axis is exactly the condition that stops `scanToPlace()`
(and everything downstream of it) from ever running. Reaching that
branch legitimately needs a manual axis reassignment (`setAxis()`) on
top of a 3+-dim variable; documented in the fixture's comment rather
than built around with an artificial reassignment, since this is a
coverage phase, not the branch's owning phase.

**A pre-existing order-dependence found incidentally, NOT introduced by
this phase and NOT fixed here (out of scope for a `view.cc` coverage
pass):** running the full suite with `--order-by=rand --rand-seed=99`
gives 5734 assertions where every other seed and the default order give
5733 -- reproducible, and confirmed present at `bf95708` (the commit
immediately before this phase's first commit, via a throwaway worktree
built from that commit) so it predates Phase 7a entirely. Isolated to
`tests/test_do_print.cc`'s `"do_print: cancelling the printer_options
dialog skips in_print entirely"` test, whose final assertion count is
`g_recorded_calls.size()` at that point (it loops `CHECK(s != "in_print")`
once per recorded call) -- meaning some earlier test, depending on
execution order, is recording one extra UI call before this test runs
that isn't being cleared between test cases the way `g_recorded_calls`
itself is. Root cause not identified beyond that; flagged for whoever
next touches `test_do_print.cc` or the `do_print.cc`/`Dataset::checkRanges`
interaction, rather than guessed at further here. Neither ordering fails
outright (`0 failed` both times) -- this is silent flakiness in what a
passing run asserts, not a crash, which is exactly the class of bug the
shuffled-order gate exists to surface.

167->216 tests overall across 7a's three files (147 tests / 3982
assertions was this plan's count at the start of Phase 4b; the plan file
has the exact running totals), full verification (4-gate + ASan/UBSan/LSan
+ shuffled order across multiple seeds) clean at every commit except for
the pre-existing flakiness just described, which is independent of any
change in this phase.

**Next: 7b** -- close Phase 6's residue (the 17 remaining direct
`netcdf_*` bypasses, and `epic_time.cc`, never migrated).

## Phase 7b: close Phase 6's residue

Small, mechanical, no behavior change. Every one of the remaining
direct `netcdf_*` bypasses the round-4 survey found was checked
individually against the actual current code before touching it, per
this plan's "verify before acting" discipline -- and one of the two
biggest findings this phase surfaced is a case where blindly following
the survey's grep would have introduced a real bug.

**Four new single-file `NetCDFFile` forwarders** (`dataset.h`/
`dataset.cc`): `charAtt`, `attString`, `dimValue`, `getData`. These
weren't part of Phase 6's 13-forwarder migration because they were never
routed through the `fi_*()`/`file.cc` dispatch layer that phase collapsed
-- no `fi_get_char_att`/`fi_att_string`/`fi_dim_value_single_file`/
`fi_get_data_single_file` ever existed. But every call site already held
the owning `NetCDFFile*` the same way the 13 did, so the same treatment
applies: one-line bodies forwarding straight to the underlying
`netcdf_*()` call, unchanged.

**`var_metadata.cc`'s 8 direct calls**: 6 migrated onto the object
(`charAtt` for the "coordinates" attribute lookup, `varSize` x2,
`dimIdToName` x2, `getData` for the coordinate-mapping-variable read),
**2 deliberately left alone and documented**: `netcdf_n_dims()` at both
call sites in `handle_dim_mapping`/`handle_dim_mapping_2d`. This is the
real finding -- `netcdf_n_dims()` (`file_netcdf.cc:837`) and
`netcdf_fi_n_dims()` (the function `->nDims()` forwards to,
`file_netcdf.cc:352`) are **two different functions**, not the same
function under two names: `netcdf_fi_n_dims()` resolves group-prefixed
variable names via `nc_inq_varid_grp()`, `netcdf_n_dims()` does a plain
`nc_inq_varid()` with no group support. The round-4 survey's grep found
both under a generic "n_dims-shaped bypass" heading and implicitly
treated them as the same target; reading both functions' actual bodies
before migrating (rather than pattern-matching the name) caught this --
routing these two call sites through `->nDims()` would have silently
changed which variable lookup runs for any coordinate-mapping variable
living inside a netCDF-4 group. Left as direct `netcdf_n_dims()` calls
with a comment explaining why, rather than migrated.

**`view.cc`'s 3 calls**: `View::information()`'s `netcdf_att_string()`
migrated onto the new `->attString()`. The other two, in
`View::checkNewData()`, **deliberately left alone**: they open a second,
throwaway `fileid` for the *same path* the tracked `NetCDFFile` already
has open, specifically to read the on-disk file's *current* size --
the tracked object's fileid is the stale, already-open handle whose
possible growth is exactly what this function exists to detect. Routing
this through the tracked object would defeat the function's own purpose.
Scoped and closed within the function via a raw `nc_close()`, not
`fi_close()`/`NetCDFFile::close()`, since it was never tracked. Documented
in place with a comment rather than left as a silent-looking exception.

**`epic_time.cc`, migrated in full** -- the one file Phase 6 skipped
entirely. `handle_time_dim()` and `months_calc_tgran()` took a bare
`int fileid` and called `netcdf_dim_value()` directly; both now take a
`NetCDFFile *file` instead (the one caller, `var_metadata.cc`'s
`fill_dim_structs()`, already had the object in hand as `file0`), and
`months_calc_tgran()`'s two `netcdf_dim_value()` calls became
`file->dimValue()`. `fi_dim_calendar()` stayed a direct call in the same
function (`fill_dim_structs`) -- it's `file.cc`'s free function, not a
`netcdf_*` bypass, out of scope here.

**`file.cc`'s `file_type` switch: left in place.** After the above, 3
functions still branch on it (`fi_initialize`, `fi_dim_calendar`,
`fi_close`) -- exactly at the plan's own stated threshold ("don't feel
obligated to remove it if fewer than 2-3 sites still route through it").
It can still only ever hold `FILE_TYPE_NETCDF`, same dead-switch
situation as Phase 6's main pass, but these three are the last vestige
of the abstraction and read as honest guard clauses rather than active
cruft; removing them would touch three genuinely-still-in-use functions
for a purely cosmetic gain. Left as-is.

Pure relocation throughout: 216 tests / 5733 assertions, unchanged, at
every commit. Full verification (4-gate + a fresh ASan/UBSan/LSan
scratch build + 2-seed shuffled order) clean. `grep -rn` confirms no
undocumented direct `netcdf_*` bypass remains in `var_metadata.cc`,
`view.cc`, or `epic_time.cc` -- the two survivors in each of the first
two files are exactly the ones documented above.

**Phase 7 (7a + 7b) is now complete.** Phase 8 (splitting `ncview.cc`),
Phase 9 (`ui/`), and Phase 10 (coverage/fuzz CI) are all available next,
fully specified, per the plan file's status table.

## Phase 8: coverage for, then split, `ncview.cc`

`parse_options()` (246 lines) and the colormap-loading path
(`initialize_colormaps()`/`init_cmap_from_file()`, ~245 lines with their
four `static` helpers) were both at zero direct test coverage. The line
count the earlier sketch used (1,599) was stale -- Phase 3f had already
split `legal_text.cc`'s GPL text out, leaving `ncview.cc` at 892 lines --
corrected before starting, per this plan's "verify before acting" rule.

**Tests first** (`tests/test_cli_options.cc`, `tests/test_colormaps.cc`),
landed and passing against the unmodified functions, confirmed by
stashing the split commit and rebuilding: same 240 tests / 6614
assertions with or without the split applied. `parse_options()` writes
directly into the global `options` struct rather than returning a value,
so tests assert against `options`' post-call state. Two real, current
quirks got pinned rather than fixed, both caught by tracing the index
arithmetic directly rather than trusting a first read: `-minmax all` and
`-minmax exh` silently map to the same `MinMaxMethod::Exhaust` (no
separate enumerator exists for "all"), and `-repl` sets `options.blowup`
(the magnitude), not `options.blowup_type`, exactly per the code's own
preserved comment. One near-miss corrected before landing: `-missvalrgb`
looked buggy on a first read (`argv[i+1]` appears three times with a
single `i++` between each) but is actually correct -- the `i++`
increments *before* each subsequent read, so r/g/b each read their own
argument. Deliberately uncovered: every `exit()` path (missing required
arguments, out-of-range `-nc`/`-maxsize`, `-w`/`-c`, an unrecognized
flag) -- calling any of these in-process kills the test binary, the same
category of gap as Phase 5a's `determine_file_type` rejection path.

`initialize_colormaps()`/`init_cmap_from_file()` are the only two
externally-declared entry points (`ncview/protos.h`); their four helpers
stay `static`, so -- matching this plan's established practice of
testing through public entry points rather than a file's private
implementation (`do_print.cc`'s `build_print_info`, `file.cc`'s
`netcdf_*` internals) -- they're exercised only indirectly, through
`initialize_colormaps()`'s directory-scanning path (an isolated `$HOME`
and a chdir'd scratch `.`, since the function unconditionally scans both
with no way to suppress either). Extended `stub_interface.cc`'s
`in_create_colormap()`/`x_seen_colormap_name()` to actually capture and
script their arguments (`g_created_colormaps`, `g_seen_colormap_names`)
instead of only recording that a call happened -- the same category of
extension as Phase 4a's `printer_options()`/`in_print()` capture, needed
because "a colormap was created" can't distinguish a correct load from a
silently wrong one. All 25 built-in colormaps' names and one's exact
content (`bw`, an exact grayscale ramp) are asserted; malformed files
(too few lines, wrong entry count, an out-of-range component) and the
duplicate-name skip are all confirmed via `init_cmap_from_file()`
directly.

**Step 2's scope, decided rather than hedged**: `options.` is referenced
242 more times across 10 other `core/src` files (`file.cc`,
`file_netcdf.cc`, `dataset.cc`, `var_metadata.cc`, `view.cc`,
`viewer_controller.cc`, `viewer_session.cc`, `render_pipeline.cc`,
`overlay.cc`, `do_print.cc`) -- grepped directly before choosing, per the
plan's standing rule. Redesigning `parse_options()` to return a populated
`StartupSettings` instead would mean redesigning `Options`' entire
reference-member wiring (`viewer_session.cc:46-57`), a project-wide API
change no single phase should absorb as a side effect of a file split.
Took the plan's explicitly-allowed minimum-viable option instead: pure
file motion, `parse_options()` still writing into `options`.
`cli_options.cc` (291 lines) gets `parse_options()` alone;
`colormap_library.cc` (330 lines) gets the two public entry points plus
their four `static` helpers and the 24 colormap-data `#include`s.
`ncview.cc` drops to 359 lines.

Incidental, out of scope, left alone: `create_default_colormap()`
(`ncview.cc`) is dead code -- declared in `protos.h`, defined, called
nowhere in production or tests except one comment. Not deleted here;
this phase's directive was coverage-then-split, not a dead-code sweep,
and it doesn't block anything.

Verified twice -- once for the tests-only commit (stashed the split,
rebuilt, ran the full suite), once for the final split state: clean
`-Werror` build; `ctest` normal + `--order-by=rand` (seeds 3, 17 -- seed
17's 6615-vs-6614 count matches the pre-existing, already-documented
`test_do_print.cc` order-dependence, not a regression);
`ncview_core_linkcheck` exit 0; all 13 `ui_smoke.sh` goldens
byte-identical; a scratch ASan/UBSan/LSan build clean (240/240, no
reports); `grep -rn` confirms no stale reference to any moved symbol
survives in `ncview.cc`. 216->240 tests, 5733->6614 assertions.

**Phase 8 is now complete.** Phase 9 (`ui/`) and Phase 10 (coverage/fuzz
CI) are both available next, fully specified, per the plan file's status
table.

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

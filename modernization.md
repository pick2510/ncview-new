# Bring `core/` up to idiomatic C++17

## Context

`core/` is ~14.5k lines across 15 `.cc` files that are still, structurally, upstream ncview's C. M1 of the port (`24ef4f2`) copied `src/*.c` → `core/src/*.cc` and made the minimum changes needed to compile as C++; PORTING.md's M1 section calls this out explicitly as "flagged as a TODO to clean up properly later". Everything since has been UI work, cross-platform build fixes, and release hardening — the core itself has never been modernized.

What that leaves today, measured:

| Signal | Count |
|---|---|
| `malloc`/`calloc`/`realloc`/`free` sites | 234 |
| `strcpy`/`strcat`/`sprintf` calls | 149 |
| `exit()` calls inside the library | 192 |
| `fprintf(stderr, …)` | 595 |
| Mutable globals (`ncview.cc:67-71`) | 5 |
| Intrusive `AnyPtr` linked lists | 4 declared, 3 live |
| `->next` / `->prev` traversal sites | 61 / 16 |
| Compiler warning flags anywhere in the build | **0** |

The build runs at compiler defaults — no `-Wall`, no `-Wextra`, no sanitizers, on any of the three CI platforms. `core/src/util.cc:232` already prints `size_t` values through `%ld`, which a warnings-clean build would have caught. `AnyPtr` (`core/include/ncview/anyptr.h`) exists solely to keep upstream's `void *next/prev` idiom compiling under Clang; it is a workaround for a C idiom, not a C++ design.

The intended outcome: `core/` becomes a C++17 library that owns its memory through RAII, expresses its data model in `std::string`/`std::vector`/`std::unique_ptr`, builds warnings-clean and sanitizer-clean, and behaves **exactly** as it does today.

## Decisions binding this work

1. **Scope**: modernize the data model, not just hygiene. `std::string` for the `char*` fields, `std::vector` for the manual arrays, `std::unique_ptr` for ownership, standard containers replacing the intrusive lists — `anyptr.h` gets deleted.
2. **Parity is strict.** Every commit leaves `ctest` output and the Xvfb screenshots byte-identical. Upstream quirks stay, including the ones PORTING.md deliberately preserved (`view.cc:show_current_dim_values()` ignoring `dim->timelike`). Defects found along the way get logged in PORTING.md and fixed in *separate, later* commits with their own tests — never folded into a refactoring commit.
3. **The seam may change.** `char*` → `const char*`/`std::string_view`/`std::string` across `interface.h` and `protos.h` is allowed; `ui/` (2345 lines, ~45 references to core types) is updated in the same commit.
4. **Out of scope this pass**: the 192 `exit()` calls stay as-is, and the 5 globals stay globals. Both are the "full redesign" option, and both are observable-behavior changes that strict parity forbids. Note them in PORTING.md as the next phase.

## Phase 0 — Build the safety net first (no core changes)

`view.cc` (3215), `ncview.cc` (1526), `do_buttons.cc` (337) and `overlay.cc` (688) — the entire display/state-machine layer, ~47% of `core/src` — have **zero** test coverage today. Refactoring them without a net is the main risk in this plan, so the net comes first.

**0-pre. Branch.** All of this work lands on a `modernization` branch, never on `master`:

```
git status                      # confirm clean; stash -u anything outstanding
git checkout -b modernization
```

Every phase below commits there. `master` stays at `ac96a46` (v0.1.0) so the release artifacts and the parity baseline remain reachable throughout — the Phase 0 goldens and screenshots are captured against it and compared against every commit on the branch.

**0a. Warnings on, not yet enforcing.** Add an `ncview_warning_flags` INTERFACE target in the top-level `CMakeLists.txt` (`-Wall -Wextra -Wno-unused-parameter`, `/W4` on MSVC) and attach it to `ncview_core`, `ncview_ui`, `app`. Do **not** set `-Werror` yet — capture the baseline warning list into `core/WARNINGS.md` instead. It becomes the phase-by-phase checklist, and each later phase deletes its lines from it. `-Werror` gets switched on at the end of Phase 6, once the list is empty.

**0b. Characterization tests** in `tests/`, all reusing `make_sample_file()` (`tests/test_file_netcdf.cc:43-94`) rather than a new fixture:

- `tests/test_pixels.cc` — `data_to_pixels()` (`core/src/util.cc:202`) is the core rendering transform and has no test at all. Drive it across all four `TRANSFORM_*` values, both `BLOWUP_REPLICATE`/`BLOWUP_BILINEAR`, `options.invert_colors` on and off, and a frame containing `_FillValue`, hashing the output `ncv_pixel` buffer. This one test is the highest-value item in the whole plan.
- `tests/test_rcfile.cc` — `.ncviewrc` round-trip through `write_state_to_file`/`read_state_from_file` (`core/src/handle_rc_file.cc`), including a fixture copy of the real upstream file with `CMAP_<name> INT 1` entries that PORTING.md's M6 section describes. The escaped on-disk format produced by `stringlist_escape_string` (`core/src/stringlist.cc:1176`) must stay byte-identical — this is the test that guards Phase 3.
- `tests/test_time_fmt.cc` — `udu_fmt_time`/`fmt_time`/`epic_fmt_time` and `udu_calc_tgran` across every calendar `calcalcs.cc` supports, not just the four `test_calendar.cc` currently covers.
- `tests/test_varlist.cc` — `add_var_to_list`/`get_var`/`add_to_varlist`/`n_vars_in_list`/`is_scannable`/`fill_dim_structs` over a multi-file virtual variable. This is the direct guard for Phase 5.

**0c. Xvfb screenshot harness.** `tests/ui_smoke.sh` scripts the env-var hooks already in `ui/src/interface_fltk.cc:60-112` (`NCVIEW_TEST_AUTOSELECT`, `NCVIEW_TEST_DIALOG=range|options|dimset|info|dataedit|plot|print|overlay`, `NCVIEW_TEST_BUTTON=<name>`) under Xvfb, captures a PNG per case into `tests/golden/`, and compares against committed goldens with an exact-pixel check. PORTING.md already documents these hooks as "worth keeping for regression checks" — they have simply never been wired into anything automated. Register as a ctest test gated on `NCVIEW_BUILD_UI_TESTS` (Linux + Xvfb only).

**0d. Sanitizer CI job.** `NCVIEW_SANITIZE=address,undefined` option in the top-level `CMakeLists.txt`, plus a fourth job in `.github/workflows/build.yml` (Linux, Debug, `-DNCVIEW_SANITIZE=address,undefined`) running `ctest`. This is PORTING.md's own verification step 5, never implemented. Expect it to fail on the first run — triage what it finds into PORTING.md, fix nothing yet. RAII in Phases 3-6 is what actually clears it.

## Phase 1 — Dead code and constants

**Delete outright** (verified unreferenced anywhere outside their own declaration):
- `Cmaplist` and `CMAPLIST_MAGIC` (`core/include/ncview/defines.h:604-615`) — the X11 colormap-cell struct from `colormap_funcs.c`, which this port deliberately never carried over. Deleting it removes one of `anyptr.h`'s four justifications for free.
- `Server_Info`, `ORDER_RGB`, `ORDER_BGR` (`defines.h:580-602`) — X server pixel-format description, meaningless once FLTK expands pixels to RGB.

**Constants.** Convert the `#define` blocks in `defines.h` to `enum class` where the values form a closed set the code switches on — `Transform`, `BlowupType`, `ShrinkMethod`, `Device`, `Overlay`, `TimeStandard`, `TimeGranularity`, `MinMaxMethod`, `ViewDataStatus`, `Dimension`, `Modifier`, `Message`, `VarselStyle` — and to `constexpr` for the plain limits (`MAX_VAR_NAME_LEN`, `DEFAULT_FILL_VALUE`, `MAX_SCALAR_COORDS`, …). Drop `TRUE`/`FALSE` in favour of `bool`.

Two deliberate exceptions, because `ui/` switches on them and their *numeric values* are part of the seam: keep `BUTTON_*` (21 values) and `LABEL_*` (13) as-is for now; they move in Phase 7 together with their `ui/` call sites.

`PseudoColor` (`defines.h:41`) stays a plain constant — it's an X11 visual-class value core compares `options.display_type` against, and its comment already explains why.

## Phase 2 — RAII and bounded strings, mechanically

Sweep the 234 allocation and 149 unsafe-string sites, module by module in the Phase 3-6 order, *without* changing any struct layout yet:

- Scratch buffers that are allocated and freed inside one function (`util.cc:229/369` `scaled_data`, `:662/753` `data`, `:821-857` `count`/`start`, `:1409-1448` `count_vals`/`unique_vals`) → `std::vector<T>`. These are pure local changes with no API impact and they clear most of the leak findings from 0d.
- `sprintf` → `snprintf` with `sizeof buf`, `strcpy`/`strcat` into fixed buffers → bounded equivalents. Several of these already have a length parameter available and ignore it.
- `static char buffer[MAX_NC_NAME]` returned to callers (`file_netcdf.cc:2119`, `:2132`, `calcalcs.cc:59`, `stringlist.cc:1137/1175`) — leave the *behavior* alone here; these get resolved properly by the `std::string` returns in Phase 4.

## Phase 3 — `Stringlist` → a real container

`Stringlist` (`core/include/ncview/stringlist.h:39-46`) is the most self-contained of the three live lists and the one most entangled with the seam, so it goes first.

Replace the intrusive list with:

```cpp
struct StringlistEntry {
    std::string string;
    int index = 0;
    std::variant<std::monostate, int, std::string, float, bool> aux;  // was void* + sltype
};
using Stringlist = std::vector<StringlistEntry>;
```

The `SLTYPE_*` tag is exactly a `std::variant` discriminant, and all five alternatives are genuinely in use (16 `NULL`, 8 `INT`, 8 `STRING`, 6 `BOOL`, 5 `FLOAT` across core and ui). `SL_MAGIC`/`SL_BAD_MAGIC` and `stringlist_check_args` (`stringlist.cc:452`) exist to detect use-after-free of list nodes and become meaningless — delete them.

This subsumes roughly half of `core/src/stringlist.cc`'s 1206 lines (`_new_sl`, `_copy_name`, `_copy_aux`, `_add_string_common`, `_delete_single_element_inner`, `_delete_entire_list`, `_len`, `_cat`, `_match_string_exact`, the manual ordered insert). **What must survive unchanged is the file format**: `stringlist_write_to_file`/`read_from_file` and the `_escape_string`/`_unescape_string`/`_line_to_sl`/`_get_tok_indices` parser (`stringlist.cc:664-1206`) keep producing and consuming byte-identical `.ncviewrc` content. `tests/test_rcfile.cc` from Phase 0b is the gate.

`Stringlist` appears in the seam (`in_set_scan_dims`, `in_popup_XY_graph`, `x_init_dim_info`, `get_persistent_X_state`) and at ~14 `ui/` sites, so `ui/src/main_window.cc`, `ui/src/plot_window.cc`, `ui/src/interface_fltk.cc` and `tests/stub_interface.cc` all change in this same commit. The `ncview_core_linkcheck` target (`tests/CMakeLists.txt:11-27`) force-links the whole archive and will catch any signature drift immediately.

## Phase 4 — `char*` returns → `std::string`

The ten `char*`-returning functions in `protos.h` (`fi_title`, `fi_long_var_name`, `fi_var_units`, `fi_dim_units`, `fi_dim_calendar`, `fi_dim_longname`, `fi_dim_id_to_name`, `netcdf_att_string`, `netcdf_global_att_string`, `netcdf_get_char_att`, `limit_string`) have no documented ownership convention — some return `malloc`'d memory, some return the `static char buffer` from Phase 2. Convert all to `std::string` returns, and their `char *name` parameters to `std::string_view`.

This is where most of `file.cc` (640) and `file_netcdf.cc` (2217) stop leaking. `tests/test_file_netcdf.cc` already covers 12 of these entry points; extend it to the rest as they convert.

## Phase 5 — The variable data model

The heart of the change. In `core/include/ncview/defines.h`:

```cpp
struct FDBlist {                         // was defines.h:287-307
    int id = 0, index = 0;
    std::string filename;
    std::unique_ptr<NetCDFOptions> aux_data;   // was void*
    std::vector<size_t> var_size;
    float data_min = 0, data_max = 0;
    std::string recdim_units;
    ut_unit *ut_unit_ptr = nullptr;      // udunits2-owned, stays raw
};

struct NCVar {                           // was defines.h:373-464
    std::string name;
    float fill_value = 0;
    bool have_set_range = false;
    std::vector<std::unique_ptr<FDBlist>> files;   // was first_file/last_file + next/prev
    std::vector<FDBlist*> timestep_2_fdb;          // non-owning index into files
    std::vector<size_t> size;
    std::vector<std::unique_ptr<NCDim>> dim;
    std::vector<std::unique_ptr<NCDim_map_info>> dim_map_info;
    std::vector<std::unique_ptr<NCDim_map_info>> scalar_dim_map_info;
    // ... scalar fields unchanged
};
```

and the `variables` global (`ncview.cc:68`) becomes `std::vector<std::unique_ptr<NCVar>>`, so `add_to_varlist`/`new_variable`/`new_fdblist` (`util.cc:110-157`) collapse into `emplace_back`. `NCDim::values` and `NCDim_map_info`'s six raw arrays (`coord_var_size`, `matching_var_dims`, `data_cache`, `index_place_factor`, …) become `std::vector`.

Two ordering constraints worth stating up front:

- `n_scalar_coords` becomes `scalar_dim_map_info.size()`, and the fixed `MAX_SCALAR_COORDS` (20) preallocation at `util.cc:473` disappears — but the *cap* must be preserved as an explicit check, since exceeding it is an upstream-defined error path.
- `dim` deliberately holds `NULL` for non-scannable dimensions (`defines.h:424-430`). That stays: `std::vector<std::unique_ptr<NCDim>>` with null entries, not a compacted list. Compacting would silently renumber `dim_id` and break parity.

The 61 `->next` traversals become range-`for`. **`anyptr.h` is deleted at the end of this phase**, along with the explanatory paragraph at `core/CMakeLists.txt:53-64`. `ui/src/main_window.cc:634` and `ui/src/interface_fltk.cc:61-69` traverse `variables` directly and change with it.

`tests/test_varlist.cc` from Phase 0b is the gate; `test_file_netcdf.cc` and the Xvfb harness cover the rest.

## Phase 6 — `View`, `FrameStore`, `Options`

- `View::data` (`void*`, always `float*` in practice) → `std::vector<float>`; `View::pixels` → `std::vector<ncv_pixel>`; `var_place` → `std::vector<size_t>`. `alloc_view_storage` (`view.cc:1466`) becomes a resize.
- `FrameStore::frame`/`frame_valid` (`defines.h:487-493`) → `std::vector`.
- `Options`: `char *ncview_base_dir/window_title/calendar` → `std::string`; `OverlayOptions::overlay` (`int*`) → `std::vector<int>`. `pixel_transform` (`ncview.cc:69`) → `std::array<ncv_pixel,256>`, which also removes the null-deref class of bug PORTING.md's M6 section describes.
- `PrintOptions::font_name[132]`/`out_file_name[1024]` (`defines.h:569-570`) → `std::string`. Note `do_print.cc`'s PostScript writer and `ui/src/main_window.cc:1188`'s dialog both touch these.

At the end of this phase `core/WARNINGS.md` should be empty; switch `-Werror` on for `ncview_core` in CI.

## Phase 7 — Seam sweep and const-correctness

Final pass over `core/include/ncview/interface.h` (48 functions) and `protos.h`: `char *message` → `std::string_view`, add `const` to every parameter that is only read, and move `BUTTON_*`/`LABEL_*` to `enum class Button`/`enum class Label` now that `ui/` can change with them. `tests/stub_interface.cc` mirrors the header line-for-line and updates in lockstep.

Also fold in the two documentation fixes this work invalidates: the stale comment at top-level `CMakeLists.txt:15-16` (it claims tests link core into a *shared library*; `tests/CMakeLists.txt:5-10` explains why it is deliberately an executable), and a PORTING.md section recording what this modernization did, what it deliberately left (the 192 `exit()` calls, the 5 globals), and the defect log from Phase 0d.

## Follow-up commits (separate, after Phase 7)

Strict parity means these are *found* during the work and fixed afterwards, each with its own test:

- `core/src/view.cc:1231` — an unguarded `printf("got an expose event\n")` in `redraw_ccontour()`, writing to stdout in normal operation.
- `core/src/util.cc:232-234` — `size_t` values printed through `%ld`, undefined on Windows/LLP64 where the CI already builds.
- Whatever 0d's sanitizer run turns up that RAII does not resolve on its own.

## Verification

Per commit:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j
ctest --test-dir build --output-on-failure          # incl. new characterization tests
tests/ui_smoke.sh --compare                          # Xvfb screenshots vs tests/golden/
```
Both must be **identical** to the pre-change run, not merely passing. `ncview_core_linkcheck` runs as part of `cmake --build` and catches seam drift at link time.

Per phase, additionally:
```
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DNCVIEW_SANITIZE=address,undefined
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
```

End to end, before declaring done: the full CI matrix (GCC/Linux, AppleClang/macOS, MinGW/Windows) green including the new sanitizer job; `NCVIEW_STATIC_LINK=ON` configured and built at least once locally, since CI never exercises it; and `./build/app/ncview` run by hand against a real multi-file dataset, walking PORTING.md's M6 parity checklist against the same screenshots.

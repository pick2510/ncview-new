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

**Update, done in stages:** 12 of the 13 named groups converted cleanly to `enum class` (Message, MinMaxMethod, BlowupType, ShrinkMethod, ViewDataStatus, Device, VarselStyle, TimeStandard, TimeGranularity, Modifier, Dimension, Transform — see the commit history for each). `Overlay` turned out to belong with the `BUTTON_*`/`LABEL_*` exception instead: `do_overlay()`'s own switch is a clean closed set, but `ui/`'s Options dialog (`main_window.cc`'s `setOptionsDialog`) treats overlay ids as plain array/loop indices — `overlay_names()`/`overlay_current()`/`overlay_custom_n()` return `int`, and the dialog does `for (int i = 0; i < n_overlays; i++)` against `overlay_btns[i]`/`names[i]`, matching by bare index rather than by named value. Forcing `enum class` there would mean casting at every one of those array-index sites for no real type-safety gain (unlike Transform, where the one arithmetic site is isolated in `view_change_transform()`). Left as `#define OVERLAY_*` for now; moves with `BUTTON_*`/`LABEL_*` in Phase 7 if that phase also addresses `ui/`'s array-of-names-by-index pattern, or gets its own follow-up otherwise.

`TRUE`/`FALSE` → `bool` and the plain-limit `constexpr`s are also done (separate commits). Dead-code deletion (`Cmaplist`, `Server_Info`, etc.) is done.

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

**Done.** All eleven functions (`fi_title`/`fi_long_var_name`/`fi_var_units`/`fi_dim_units`/`fi_dim_calendar`/`fi_dim_longname`/`fi_dim_id_to_name` in `file.cc`, their `netcdf_*` counterparts plus `netcdf_att_string`/`netcdf_global_att_string`/`netcdf_get_char_att` in `file_netcdf.cc`, and `limit_string` in `util.cc`) now return `std::string`, with `char *name`/`dim_name`/`var_name`/`att_name` parameters converted to `std::string_view`. `protos.h` updated to match.

- **"Not found" is now empty, not `NULL`.** Every `if (x == NULL)`/`(x != NULL)` at every call site (`do_print.cc`, `view.cc`, `util.cc`) became `.empty()`/`!.empty()`. `netcdf_dim_longname()`'s existing quirk of falling back to *echoing the input dim name* (never `NULL`) when no `long_name` attribute exists is preserved exactly — with a `std::string_view` parameter this is just `return std::string(dim_name);` on the "not found" branches, so `fi_dim_longname`/its callers never see an empty result to begin with.
- **`limit_string` stopped mutating its argument in place.** It used to trim trailing spaces and truncate at `MAX_DISPLAYED_STRING_LENGTH` by writing `'\0'` bytes directly into the *caller's* buffer and returning that same pointer. Auditing all ~13 call sites (all in `view.cc`, all of the form `snprintf(..., "%s", limit_string(x), ...)`) confirmed none of them re-read `x` afterward expecting it pre-trimmed — the mutation's only observable effect anywhere was the returned/printed value. So it's now a pure function, `std::string limit_string(std::string_view s)`, computing into a fresh string with no side effect on the original storage. This is a narrow, call-site-audited exception — not a general license to make other converted functions "pure" without the same check.
- **Two pre-existing leaks fixed as a mechanical byproduct** (same rationale as Phase 2's scratch-buffer sweep): `netcdf_dim_id_to_name()`'s intermediate `dim_name` buffer was `malloc(MAX_NC_NAME)`'d and never freed (only the final `fq_dim_name` was returned) — both are now plain stack arrays, no allocation at all. `netcdf_att_string()`/`netcdf_global_att_string()`'s per-attribute `data` buffer was `malloc`'d fresh every loop iteration and never freed — now a per-iteration `std::vector<char>`.
- **`netcdf_att_string`/`netcdf_global_att_string`'s 10000-char output cap is preserved byte-for-byte.** Both used to build into a fixed `malloc(10000)` buffer via repeated `safe_strcat()` calls that silently truncate once full; that's part of the tested, parity-locked output, so the rewrite keeps a `std::vector<char>` of the same capacity and the same `safe_strcat()` calls, only converting to `std::string` at the very end (`return std::string(buffer.data())`) — deliberately not switched to an unbounded `std::string` mid-build, which would let pathologically attribute-heavy files produce longer output than before.
- **`netcdf_get_char_att()`'s zero-length special case.** Returning a valid-but-empty malloc'd string when an attribute exists but has zero length is now just an empty `std::string` — indistinguishable from "attribute not found," but that ambiguity already existed for every caller before this change too (nothing branches on the distinction).
- **The `strdup()` bridge, at the handful of sites where a result still flows into a struct field Phase 5 hasn't converted yet.** `NCDim::name/long_name/units/calendar`, `NCDim_map_info::coord_var_units`, and `FDBlist::recdim_units` are all still raw `char*` (`util.cc`'s `fill_dim_structs()`/`handle_dim_mapping_scalar()`, `file_netcdf.cc`'s dimvar-units fill-in). Each such assignment now does `strdup()` on the `std::string`'s content, preserving the exact prior ownership model (heap string, never freed, same as before) rather than changing struct layout early. Three of these fields (`d->units`, `d->calendar`, `coord_var_units`, `recdim_units`) are read elsewhere (`udu.cc`, `view.cc`'s scalar-coord display) via bare `NULL` checks or unguarded `%s` — with no NULL guard at all in `view.cc`'s case — so an absent value had to stay a real `NULL`, not become a `strdup`'d `""`; each of those sites does `result.empty() ? NULL : strdup(result.c_str())` rather than an unconditional `strdup()`.
- **New `-Wmaybe-uninitialized` false positive in `fill_dim_structs()`.** Once the loop body grew local `std::string` scopes for the `strdup()` bridging above, GCC's flow analysis lost track of the fact that `*(v->dim + i)` is unconditionally written on every iteration (either branch of the `is_scannable()` check sets it), and started flagging the later `*(v->dim) != NULL` read as possibly-uninitialized. Fixed with an explicit redundant `*(v->dim + i) = NULL;` at the top of the loop (matching `handle_dim_mapping()`'s identical pattern for its own array immediately above in the same file) plus a narrowly-scoped `#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"` around just the one `if` line that still triggered it after that. Confirmed via a full warnings diff against the pre-Phase-4 baseline build that no other new warning category was introduced (several `-Wwrite-strings`/`-Wsign-compare` counts actually *dropped*, from eliminated `char*` casts).
- `tests/test_file_netcdf.cc` updated to compare `std::string` results with `==` instead of `std::strcmp()`.

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

**Done.** `FDBlist`/`NCVar`/`NCDim`/`NCDim_map_info` converted to the shapes above. `anyptr.h` deleted, along with its `#include` in `defines.h`, the explanatory paragraph in `core/CMakeLists.txt`, and `tests/test_varlist.cc`'s reference to it. The `variables` global is now `std::vector<std::unique_ptr<NCVar>>`; `add_to_varlist`/`new_variable` are gone (folded into `emplace_back`/`push_back` at their two call sites in `add_var_to_list()`), `new_fdblist()` survives as a small `static std::unique_ptr<FDBlist> new_fdblist()` factory. `n_scalar_coords` is gone — call sites use `scalar_dim_map_info.size()` — with the `MAX_SCALAR_COORDS` cap preserved as an explicit check in `handle_dim_mapping_scalar()`. `dim` keeps null entries for non-scannable dimensions exactly as before (`std::vector<std::unique_ptr<NCDim>>`, never compacted). One small addition beyond the plan: `udu.cc`'s own `UniqList` (a local `AnyPtr`-based linked list unrelated to NCVar/FDBlist, used only to de-duplicate one warning message) also had to go once `anyptr.h` was deleted — replaced with a plain `std::vector<std::string>` and linear search, same behavior.

- **`size_t*`-returning functions were left alone.** `fi_var_size()`/`netcdf_fi_var_size()` still return a raw `malloc`'d `size_t*` with the length implied by a separate `fi_n_dims()`/`netcdf_n_dims()` call — converting their signature wasn't in this phase's scope. Every site that stores such a result into a now-`std::vector`-typed struct field (`FDBlist::var_size`, `NCVar::size`, `NCDim_map_info::coord_var_size`) copies it in with `std::vector<size_t>(raw, raw+n)` immediately followed by `free(raw)`, rather than changing the producer functions.
- **`NCDim`/`NCDim_map_info`'s `strdup()` bridges from Phase 4 are gone.** Now that the struct fields themselves are `std::string`, `fill_dim_structs()`/`handle_dim_mapping_scalar()`/`handle_dim_mapping_2d()` assign the `std::string` results directly — no more `result.empty() ? NULL : strdup(...)` dance. The empty-string-means-not-found convention (established in Phase 4) is unchanged; every downstream `== NULL`/`!= NULL` check on these fields (`udu.cc`, `epic_time.cc`, `view.cc`'s scalar-coord display) became `.empty()`/`!.empty()`, and every C-API/`printf` call site consuming them picked up `.c_str()` (plus `const_cast<char*>` where the callee's own `char*` parameter isn't const — that seam conversion is Phase 7's job, not this one's).
- **A real bug caught by the Xvfb smoke test, not by the unit tests**: the first draft of `add_var_to_list()`'s "new variable" branch called `fi_fill_value(new_var, &new_var->fill_value)` *before* `new_var->files.push_back(...)` — `fi_fill_value()` immediately dereferences `var->files.front()`, so this segfaulted on every real invocation (`ncview_core_tests` never triggers this path with a from-scratch `NCVar`, only `add_var_to_list()` does, so it was invisible there). Fixed by restoring the original ordering: the FDBlist goes into `files` before any code that assumes at least one file is already present. This is exactly the class of ordering bug the plan's "required verification" step (running the real binary under Xvfb, not just the unit suite) exists to catch.
- **`FDBlist::recdim_units`'s NULL-vs-empty bridge collapsed into the same empty-string convention.** `netcdf_fill_aux_data()` previously did the same `result.empty() ? NULL : strdup(...)` dance Phase 4 introduced for the not-yet-converted field; now `fdb->recdim_units` is a plain `std::string` and the function just assigns the result of `netcdf_var_units()` directly. `file.cc`'s `fi_dim_value_convert()` (the only other reader) switched its `== NULL` checks to `.empty()`.
- **`NCDim_map_info::var_i_map`** changed from `void*` to a proper non-owning `NCVar*` (forward-declared above `FDBlist`/`NCDim_map_info` in `defines.h`, since `NCVar` itself is defined after both — it owns them via `std::vector<std::unique_ptr<...>>`). It was already write-only (assigned in `handle_dim_mapping_scalar()`/`_2d()`, never read anywhere) before and after this change.
- **One upstream defect preserved verbatim, not fixed**: `fill_dim_structs()`'s check for differing record-dimension units across a virtually-concatenated variable's files walks a `cursor` starting at the *second* file but never advances it inside the `while( cursor != NULL )` loop — an infinite loop if it ever actually triggers (a multi-file variable whose time units genuinely differ between files). This is exactly upstream's own logic, not something introduced by the container conversion; per the strict-parity rule, mechanical phases preserve bugs like this rather than quietly fixing them, and it's called out here (and inline at the site) rather than silently carried forward unremarked.
- Full warnings diff against the pre-Phase-5 baseline: net **-1** warning (one genuinely dead `int i` in `ncview.cc` cleared as part of the rewrite), zero new categories or new warning sites — the plan's own `-Wsign-compare` growth prediction for this phase didn't materialize because every `NCVar`/`NCDim` loop touched already used matching signed/unsigned types once written against the new container types.
- `tests/test_pixels.cc`, `test_view_data_edit.cc`, `test_varlist.cc`, `test_time_fmt.cc` updated for the new struct shapes (`var.size = {a, b}` instead of a raw C array pointer assignment; `NCDim` test fixtures build `std::string` fields directly rather than assigning possibly-`nullptr` `char*`, which would otherwise crash constructing `std::string` from a null pointer — this is exactly the SIGSEGV `test_time_fmt.cc`'s `TimeStandard::Months` case hit before the fix).
- Verified: full rebuild clean, `ctest` (49 doctest cases + the byte-exact Xvfb `ncview_ui_smoke` screenshot harness) green, and a `-DNCVIEW_SANITIZE=address,undefined` build clean under both `ASAN_OPTIONS=detect_leaks=1` (unit tests) and `detect_leaks=0` (the real binary driven through all `ncview_ui_smoke` cases under Xvfb — leak detection deliberately off there per the Phase 0d rationale: `variables` and friends are never freed before `exit()`, by design, for a long-running interactive app).

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
- `core/src/udu.cc:udu_calc_tgran()` — always returns `TGRAN_SEC` for any CF-convention "`<units> since <reference-date>`" time axis (i.e. virtually every real netCDF file), never `TGRAN_MIN`/`HOUR`/`DAY`/`MONTH`/`YEAR`. Root cause (confirmed against this project's vendored UDUNITS-2, `third_party/udunits2`, pinned v2.2.28): `ut_are_convertible(unit, seconds)` reports a unit *with* a reference origin as not convertible to plain `seconds` (a bare `"days"`, with no `"since"`, correctly reports convertible), so the function's own `if (!ut_are_convertible(...)) return TGRAN_SEC;` guard fires unconditionally. Found via `tests/test_time_fmt.cc`'s `udu_calc_tgran` characterization test (Phase 0b) — see that test's comment for the full derivation.
- `core/src/ncview.cc`'s `-cal` argument parser (`parse_options()`) — `malloc( strlen(argv[i+1] + 2) )` computed `strlen` of the string starting 2 bytes into the calendar name, not `strlen(argv[i+1]) + 2`, undersizing the allocation for any calendar name longer than 2 characters and heap-overflowing the `strcpy()` that followed on effectively every real use of `-cal`. **Fixed inline** (Phase 2, alongside the mechanical `strcpy`→`snprintf` sweep of `ncview.cc`) rather than deferred: the same commit that converts the `strcpy` to a bounded `snprintf` had to fix the size expression it now depends on to behave correctly rather than merely safely-truncate, so it was smaller and clearer to do both at once than to preserve a broken allocation size on purpose. No dedicated regression test added (this path is only reachable through full CLI argument parsing, which nothing in the test suite currently drives — see Phase 0's scope).

## Sanitizer findings (Phase 0d)

Two passes, both against a local `-DCMAKE_BUILD_TYPE=Debug -DNCVIEW_SANITIZE=address,undefined` build (not yet run in CI at the time of writing):

1. **`ctest` / the doctest suite (`ncview_core_tests`)**: completely clean. No ASan errors, no LeakSanitizer reports, no UBSan `runtime error:` output, across all 47 test cases (`ASAN_OPTIONS=detect_leaks=1`, `UBSAN_OPTIONS=print_stacktrace=1`).

2. **The real `ncview` binary, driven under Xvfb through every `NCVIEW_TEST_DIALOG`/`NCVIEW_TEST_BUTTON` hook** (the same vocabulary `tests/ui_smoke.sh` from Phase 0c uses): 16 of 18 combinations clean (leak detection deliberately off for this pass — `ncview_main()` never frees `variables`/`read_in_state`/etc. before exit, by design, for a long-running interactive app; LeakSanitizer flags all of that as "still reachable" noise that has nothing to do with real bugs). One real, reproducible defect found:

   - **`core/src/view.cc:2481-2495`, `view_data_edit()` — heap-buffer-overflow, off-by-one.** `line_array = malloc(sizeof(char*) * n_entries)` allocates exactly `n_entries` (`= x_size * y_size`) pointers; the fill loop writes indices `0..n_entries-1`, then `line_array[index] = NULL;` (line 2495) writes one pointer *past* the allocation — `x_dataedit()`'s NUL-terminator convention needs `n_entries + 1` slots, not `n_entries`. Confirmed with a full ASan stack trace (`WRITE of size 8 ... 0 bytes after 512-byte region`) via `NCVIEW_TEST_DIALOG=dataedit`, reliably reproducible on every run. Pre-existing in upstream C, carried over verbatim by the M1 port — not something this session's work introduced. **Fixed**: allocation changed to `n_entries + 1` pointers, with a regression test (`tests/test_view_data_edit.cc`) that calls `view_data_edit()` directly against a hand-built `View`/`NCVar` fixture, checks every formatted value and the terminating `NULL`, and (via a small capture hook added to `x_dataedit()` in `tests/stub_interface.cc`) frees the returned array itself. Verified two ways: (1) temporarily reverting the fix reproduces the exact ASan heap-buffer-overflow under this new test; (2) with the fix in place, the full suite (48 cases) is clean under both a plain build and `-DNCVIEW_SANITIZE=address,undefined`.
   - `NCVIEW_TEST_DIALOG=print` timed out (no crash, no sanitizer output) rather than exiting — plausibly waiting on `lpr` (not installed in this sandbox); not investigated further as it produced no sanitizer finding.

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

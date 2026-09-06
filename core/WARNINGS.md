# `core/` warning baseline (modernization.md Phase 0a, updated after the post-Phase-7 dead code cleanup)

Captured with `-Wall -Wextra -Wno-unused-parameter` (GCC 14, Debian trixie),
`RelWithDebInfo`, from a clean build.

This file is the phase-by-phase checklist `modernization.md` refers to: each
phase that touches a category below should clear every line in it and delete
that section here. The original Phase 0a plan called for this file to be
empty by the end of Phase 6, with `-Werror` switched on for `ncview_core` at
that point; Phase 6 deferred that to Phase 7 instead, on the theory that
Phase 7's `interface.h`/`protos.h` seam sweep (`char *` -> `const char*`/
`std::string_view`) would clear `-Wwrite-strings` and `-Wsign-compare` in
bulk.

That did not fully happen. Phase 7's actual scope -- the seam's function
*signatures* (`interface.h`, `protos.h`'s `in_variable_selected`/
`in_button_pressed`/`in_error`/`get_var`, and the `BUTTON_*`/`LABEL_*` ->
`enum class Button`/`Label` conversion) -- did remove a real slice of
`-Wwrite-strings` (157 -> 136: every string-literal argument that used to
need an explicit `char*` cast or a non-const local to satisfy an `in_*`/
`x_*` parameter now binds directly to a `const char*` parameter, so those
casts and the warnings they produced are gone; see e.g. `in_error(...)`'s
and `x_error(...)`'s calls, and `in_set_label`'s `Label::Title` conversion,
which dropped a `const_cast` in `view.cc`). But the bulk of both categories
lives somewhere else entirely: `-Wwrite-strings`'s remaining 136 hits are
almost all string-literal *tables and locals* (`calcalcs.cc`'s month/unit
name arrays, `ncview.cc`'s colormap-name and help-text tables,
`file_netcdf.cc`'s attribute-name literals), not seam parameters -- fixing
those means retyping `char *foo[] = {...}` declarations to
`const char *foo[]` (or `std::string`) throughout those files, a mechanical
sweep with its own scope and risk profile, not a "ripple effect" of the
interface seam. Likewise `-Wsign-compare`'s 71 hits are loop-index
signedness mismatches internal to `util.cc`/`overlay.cc`/`view.cc`/
`file_netcdf.cc` (mixing `int`/`long` loop counters against `size_t`-typed
array sizes) -- unrelated to any `char*` parameter type and not part of the
seam this phase touched.

Since Phase 7's own scope statement (`modernization.md`) was explicit that
this is a seam-and-const-correctness pass, not a whole-codebase sweep,
clearing the remaining categories is left as further follow-up work (not
labeled as a numbered phase in `modernization.md`, since the plan doesn't
define one) rather than done here under the Phase 7 banner.

A separate, small dead-code cleanup pass after Phase 7 (see
`modernization.md`'s "Dead code cleanup" section) cleared the three
remaining categories that actually were dead code rather than type/format
mismatches: `-Wunused-variable` (13), `-Wunused-but-set-variable` (9), and
`-Waddress` (1). `-Werror` is still **not** enabled for `ncview_core`: 214
warnings remain, all `-Wwrite-strings`/`-Wsign-compare`/etc. in files
neither Phase 7 nor the cleanup pass touched.

Regenerate with:
```
rm -rf build/core build/ui build/app build/tests
cmake --build build -j 2>&1 | grep -E 'warning:' \
  | sed -E 's#/home/strebdom/git/ncview-new/##' | sort -u
```

## Summary

| Category | Count | Files | Cleared by |
|---|---:|---|---|
| `-Wwrite-strings` | 136 | calcalcs.cc, file_netcdf.cc, ncview.cc, overlay.cc, util.cc, view.cc | A dedicated string-literal-table sweep (`char *foo[]` -> `const char *foo[]`/`std::string`) -- not part of Phase 7's seam scope |
| `-Wsign-compare` | 71 | do_print.cc, file_netcdf.cc, overlay.cc, util.cc, view.cc | A dedicated loop-index retyping sweep (`int`/`long` -> `size_t` to match the arrays being walked) -- not part of Phase 7's seam scope |
| `-Wstringop-truncation` | 3 | view.cc | Alongside a future `char *` -> `std::string` sweep of the fixed local buffers these `strncpy`/`strncat` calls format into |
| `-Wparentheses` | 1 | file_netcdf.cc:1124 | `if (x = f())` assignment-as-condition, needs a read to confirm intent |
| `-Wempty-body` | 1 | view.cc:582 | Trivial brace fix |
| `-Wformat-truncation=` | 1 | file_netcdf.cc:491 | Bounded `snprintf` into a fixed buffer, same family as the `-Wstringop-truncation` line above |
| `-Wformat=` | 1 | view.cc:707 | `printf("%d", view->data_status)` where `data_status` is `ViewDataStatus`, an `enum class` |

## `-Wwrite-strings` (136) — string literal assigned/passed as `char*`

core/src/calcalcs.cc:783,784,785,786,787,788,789,790,791,792,793,794,795,796,797,798,799,800,801,802,803,804,805,806,807,808,809,810,811,812,813,814,815,816 (two conversions per line: 68 total)
core/src/file_netcdf.cc:946,1177,1424,1426,1428,1430,1432,1684,1691,1699,1832,1834,1836,1838,1840,1842,1844,1912,1926,1980,1994,2026
core/src/ncview.cc:456,527,528,529,533,534,535,536,537,538,539,540,541,542,546,547,548,549,550,551,552
core/src/overlay.cc:37,38,39,40,41,307
core/src/util.cc:68 (six conversions),69 (six conversions),1468,1469,1470,1471
core/src/view.cc:381,392

## `-Wsign-compare` (71) — `size_t` vs. `int`/`long` comparison

core/src/do_print.cc:180,181
core/src/file_netcdf.cc:610,619,1274,1522,1534,1546,1558,1914
core/src/overlay.cc:158,169,207,236,282,329,499,506,583,594,600 (two per line)
core/src/util.cc:201,287,294,650,1315,1357,1448,1462,1520,1521,1526,1528,1587 (two per line),1594,1595,1603,1623,1624,1671,1672,1713,1724,1725,1738,1744,1781,1796,1811,1826,1905,1906,2109,2112,2198,2250,2313
core/src/view.cc:642,720,721,732,843,866,1899,1900,2258,2443,2444,2798

## `-Wstringop-truncation` (3)

core/src/view.cc:2172,2183,2263

## `-Wparentheses` (1)

core/src/file_netcdf.cc:1124

## `-Wempty-body` (1)

core/src/view.cc:582

## `-Wformat-truncation=` (1)

core/src/file_netcdf.cc:491

## `-Wformat=` (1)

core/src/view.cc:707

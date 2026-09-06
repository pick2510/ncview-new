# `core/` warning baseline (modernization.md Phase 0a, updated after Phase 7)

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
define one) rather than done here under the Phase 7 banner. `-Werror` is
**not** enabled for `ncview_core`: 237 warnings remain, concentrated in
files this phase deliberately did not touch.

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
| `-Wunused-variable` | 13 | file_netcdf.cc, util.cc | Mechanical -- delete outright, no behavior to preserve |
| `-Wunused-but-set-variable` | 9 | do_print.cc, file_netcdf.cc, udu.cc, util.cc, view.cc | Verify each is genuinely dead (several are netCDF status codes never checked, matching upstream) before deleting |
| `-Wstringop-truncation` | 3 | view.cc | Alongside a future `char *` -> `std::string` sweep of the fixed local buffers these `strncpy`/`strncat` calls format into |
| `-Wparentheses` | 1 | file_netcdf.cc:1126 | `if (x = f())` assignment-as-condition, needs a read to confirm intent |
| `-Wempty-body` | 1 | view.cc:582 | Trivial brace fix |
| `-Waddress` | 1 | file_netcdf.cc:488 | `&groupname` where `groupname` is an array, always true; delete the dead check after confirming it guards nothing else |
| `-Wformat-truncation=` | 1 | file_netcdf.cc:491 | Bounded `snprintf` into a fixed buffer, same family as the `-Wstringop-truncation` line above |
| `-Wformat=` | 1 | view.cc:707 | `printf("%d", view->data_status)` where `data_status` is `ViewDataStatus`, an `enum class` |

## `-Wwrite-strings` (136) — string literal assigned/passed as `char*`

core/src/calcalcs.cc:783,784,785,786,787,788,789,790,791,792,793,794,795,796,797,798,799,800,801,802,803,804,805,806,807,808,809,810,811,812,813,814,815,816 (two conversions per line: 68 total)
core/src/file_netcdf.cc:946,1179,1426,1428,1430,1432,1434,1686,1693,1701,1834,1836,1838,1840,1842,1844,1846,1914,1928,1982,1996,2028
core/src/ncview.cc:454,525,526,527,531,532,533,534,535,536,537,538,539,540,544,545,546,547,548,549,550
core/src/overlay.cc:37,38,39,40,41,307
core/src/util.cc:68 (six conversions),69 (six conversions),1470,1471,1472,1473
core/src/view.cc:381,392

## `-Wsign-compare` (71) — `size_t` vs. `int`/`long` comparison

core/src/do_print.cc:180,181
core/src/file_netcdf.cc:610,619,1276,1524,1536,1548,1560,1916
core/src/overlay.cc:158,169,207,236,282,329,499,506,583,594,600 (two per line)
core/src/util.cc:201,287,294,650,1317,1359,1450,1464,1522,1523,1528,1530,1589 (two per line),1596,1597,1605,1625,1626,1673,1674,1715,1726,1727,1740,1746,1783,1798,1813,1828,1907,1908,2111,2114,2200,2253,2317
core/src/view.cc:642,720,721,732,843,866,1899,1900,2258,2443,2444,2798

## `-Wunused-variable` (9)

core/src/file_netcdf.cc:163 (n_groups), 1090 (gn_slash), 1094 (parent_id), 1095 (id1, id2, id3), 1779 (n_vars), 1781 (n_gatts, rec_dim), 2121 (tlen)
core/src/util.cc:2312 (i0, i1), 2313 (ts)

## `-Wunused-but-set-variable` (9)

core/src/do_print.cc:222 (istat)
core/src/file_netcdf.cc:80 (dummyerr), 2107 (ierr), 2120 (ierr), 2135 (ierr)
core/src/udu.cc:130 (rettype)
core/src/util.cc:1306 (sum), 2216 (ierr)
core/src/view.cc:759 (ierr)

## `-Wstringop-truncation` (3)

core/src/view.cc:2172,2183,2263

## `-Wparentheses` (1)

core/src/file_netcdf.cc:1126

## `-Wempty-body` (1)

core/src/view.cc:582

## `-Waddress` (1)

core/src/file_netcdf.cc:488

## `-Wformat-truncation=` (1)

core/src/file_netcdf.cc:491

## `-Wformat=` (1)

core/src/view.cc:707

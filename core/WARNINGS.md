# `core/` warning baseline (modernization.md Phase 0a, updated after Phase 6)

Captured with `-Wall -Wextra -Wno-unused-parameter` (GCC 14, Debian trixie),
`RelWithDebInfo`, from a clean build.

This file is the phase-by-phase checklist `modernization.md` refers to: each
phase that touches a category below should clear every line in it and delete
that section here. The original Phase 0a plan called for this file to be
empty by the end of Phase 6, with `-Werror` switched on for `ncview_core` at
that point. That did not happen: Phase 6's actual scope (`View`,
`FrameStore`, `Options`, `PrintOptions` -> RAII containers) touches none of
the warning categories below in bulk -- they are concentrated in
`calcalcs.cc`'s string-literal tables, `file_netcdf.cc`'s netCDF C-API glue,
and loop-index signedness across `util.cc`/`overlay.cc`/`view.cc`, none of
which Phase 6 mechanically changes. Clearing `-Wwrite-strings` in bulk
needs `core/include/ncview/interface.h`'s `char *message` -> `std::string_view`
sweep, and clearing `-Wsign-compare` needs the loop-index retyping that
comes with it -- both are explicitly **Phase 7** scope
(`modernization.md`'s "Seam sweep and const-correctness"), so `-Werror` is
**not** enabled here; that is now deferred to the end of Phase 7 instead.

Regenerate with:
```
rm -rf build/core build/ui build/app build/tests
cmake --build build -j 2>&1 | grep -E 'warning:' \
  | sed -E 's#/home/strebdom/git/ncview-new/##' | sort -u
```

## Summary

| Category | Count | Files | Cleared by |
|---|---:|---|---|
| `-Wwrite-strings` | 157 | defines.h, calcalcs.cc, file_netcdf.cc, ncview.cc, overlay.cc, util.cc, view.cc | Phase 7's `interface.h` seam sweep (`char *message` -> `std::string_view`) plus the remaining string-literal arrays/locals |
| `-Wsign-compare` | 71 | do_print.cc, file_netcdf.cc, overlay.cc, util.cc, view.cc | Phase 7, as loop indices become `size_t`-typed alongside the arrays they walk |
| `-Wunused-variable` | 13 | file_netcdf.cc, util.cc | Phase 7 mechanical sweep -- delete outright, no behavior to preserve |
| `-Wunused-but-set-variable` | 9 | do_print.cc, file_netcdf.cc, udu.cc, util.cc, view.cc | Phase 7 -- verify each is genuinely dead (several are netCDF status codes never checked, matching upstream) before deleting |
| `-Wstringop-truncation` | 3 | view.cc | Phase 7, alongside `interface.h`'s `char *` -> `std::string_view` sweep (the truncating calls are formatting into fixed local buffers that feed those parameters) |
| `-Wparentheses` | 1 | file_netcdf.cc:1126 | Phase 7 -- `if (x = f())` assignment-as-condition, needs a read to confirm intent |
| `-Wempty-body` | 1 | view.cc:582 | Phase 7 -- trivial brace fix |
| `-Waddress` | 1 | file_netcdf.cc:488 | Phase 7 -- `&groupname` where `groupname` is an array, always true; delete the dead check after confirming it guards nothing else |
| `-Wformat-truncation=` | 1 | file_netcdf.cc:491 | Phase 7 -- not in the original Phase 0a capture (an omission, not a regression); bounded `snprintf` into a fixed buffer, same family as the `-Wstringop-truncation` line above |
| `-Wformat=` | 1 | view.cc:707 | Phase 7 -- `printf("%d", view->data_status)` where `data_status` is `ViewDataStatus`, an `enum class`; not in the original Phase 0a capture (an omission, not a regression) |

## `-Wwrite-strings` (157) — string literal assigned/passed as `char*`

core/include/ncview/defines.h:45
core/src/calcalcs.cc:783,784,785,786,787,788,789,790,791,792,793,794,795,796,797,798,799,800,801,802,803,804,805,806,807,808,809,810,811,812,813,814,815,816 (two conversions per line: 68 total)
core/src/file_netcdf.cc:946,1179,1426,1428,1430,1432,1434,1686,1693,1701,1834,1836,1838,1840,1842,1844,1846,1914,1928,1982,1996,2028
core/src/ncview.cc:454,525,526,527,531,532,533,534,535,536,537,538,539,540,544,545,546,547,548,549,550,777
core/src/overlay.cc:37,38,39,40,41,61,97,205,307
core/src/util.cc:68 (six conversions),69 (six conversions),1470,1471,1472,1473,1931,1933
core/src/view.cc:381,392,1239,1347,1356,2058,2065,2511,2551,2581,2755,2761,2857,3109,3110,3111,3112

## `-Wsign-compare` (71) — `size_t` vs. `int`/`long` comparison

core/src/do_print.cc:180,181
core/src/file_netcdf.cc:610,619,1276,1524,1536,1548,1560,1916
core/src/overlay.cc:158,169,207,236,282,329,499,506,583,594,600 (two per line)
core/src/util.cc:201,287,294,650,1317,1359,1450,1464,1522,1523,1528,1530,1589 (two per line),1596,1597,1605,1625,1626,1673,1674,1715,1726,1727,1740,1746,1783,1798,1813,1828,1907,1908,2111,2114,2200,2253,2317
core/src/view.cc:642,720,721,732,843,866,1899,1900,2260,2445,2446,2800

## `-Wunused-variable` (13)

core/src/file_netcdf.cc:163 (n_groups), 1090 (gn_slash), 1094 (parent_id), 1095 (id1, id2, id3), 1779 (n_vars), 1781 (n_gatts, rec_dim), 2121 (tlen)
core/src/util.cc:2312 (i0, i1), 2313 (ts)

## `-Wunused-but-set-variable` (9)

core/src/do_print.cc:222 (istat)
core/src/file_netcdf.cc:80 (dummyerr), 2107 (ierr), 2120 (ierr), 2135 (ierr)
core/src/udu.cc:130 (rettype)
core/src/util.cc:1306 (sum), 2216 (ierr)
core/src/view.cc:759 (ierr)

## `-Wstringop-truncation` (3)

core/src/view.cc:2174,2185,2265

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

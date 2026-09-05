# `core/` warning baseline (modernization.md Phase 0a)

Captured with `-Wall -Wextra -Wno-unused-parameter` (GCC 14, Debian trixie),
`RelWithDebInfo`, from a clean build right after adding `ncview_warning_flags`
in the top-level `CMakeLists.txt`. `ui/` and `app/` are already clean — every
one of the 274 warnings below is in `core/`.

This file is the phase-by-phase checklist `modernization.md` refers to: each
phase that touches a category below should clear every line in it and delete
that section here. Once this file is empty, `-Werror` goes on for
`ncview_core` (end of Phase 6, per the plan).

Regenerate with:
```
rm -rf build/core build/ui build/app build/tests
cmake --build build -j 2>&1 | grep -E 'warning:' \
  | sed -E 's#/home/strebdom/git/ncview-new/##' | sort -u
```

## Summary

| Category | Count | Files | Cleared by |
|---|---:|---|---|
| `-Wwrite-strings` | 165 | calcalcs.cc, file_netcdf.cc, ncview.cc, overlay.cc, util.cc, view.cc, stringlist.cc, handle_rc_file.cc, defines.h | Phase 4 (`std::string`/`std::string_view` returns and params) — the handful not covered by Phase 4 (string-literal arrays passed to `in_error`/`x_error`, `char *msg` locals) get `const char*`/`std::string_view` in Phase 7's seam sweep |
| `-Wsign-compare` | 76 | do_print.cc, file_netcdf.cc, overlay.cc, stringlist.cc, util.cc, view.cc | Phase 5/6, as loop indices and sizes become `size_t`-typed (`std::vector::size()`) alongside the arrays they walk |
| `-Wunused-variable` | 15 | file_netcdf.cc, ncview.cc, util.cc | Phase 2 (mechanical sweep) — delete outright, no behavior to preserve |
| `-Wunused-but-set-variable` | 10 | do_print.cc, file_netcdf.cc, udu.cc, util.cc, view.cc | Phase 2 — same as above; verify each is genuinely dead (a couple are netCDF status codes never checked, matching upstream) before deleting |
| `-Wstringop-truncation` | 4 | view.cc | Phase 2 (bounded replacements) or Phase 6 if the field involved becomes `std::string` first — check case by case |
| `-Wparentheses` | 1 | file_netcdf.cc:1135 | Phase 2 — `if (x = f())` assignment-as-condition, needs a read to confirm intent before adding the extra parens/`==` |
| `-Wempty-body` | 1 | view.cc:578 | Phase 2 — trivial brace fix |
| `-Waddress` | 1 | file_netcdf.cc:493 | Phase 2 — `&groupname` where `groupname` is an array, always true; delete the dead check after confirming it guards nothing else |

## `-Wwrite-strings` (165) — string literal assigned/passed as `char*`

core/include/ncview/defines.h:43
core/src/calcalcs.cc:783,784,785,786,787,788,789,790,791,792,793,794,795,796,797,798,799,800,801,802,803,804,805,806,807,808,809,810,811,812,813,814,815,816 (two conversions per line: 30 and 36)
core/src/file_netcdf.cc:1051,1057,1070,1188,1435,1437,1439,1441,1443,1704,1711,1719,1852,1854,1856,1858,1860,1862,1864,1926,1940,1995,2009,2041,956
core/src/handle_rc_file.cc:62
core/src/ncview.cc:454,525,526,527,531,532,533,534,535,536,537,538,539,540,544,545,546,547,548,549,550,780
core/src/overlay.cc:37,38,39,40,41,61,97,205,274,313,358
core/src/stringlist.cc:953
core/src/util.cc:66 (six conversions),67 (six conversions),959,1591,1592,1593,1594,2053,2055
core/src/view.cc:377,388,1252,1354,1363,2098,2105,2549,2589,2619,2792,2798,2894,3139,3140,3141,3142

## `-Wsign-compare` (76) — `size_t` vs. `int`/`long` comparison

core/src/do_print.cc:180,181
core/src/file_netcdf.cc:619,628,990,1288,1533,1545,1557,1569,1928
core/src/overlay.cc:158,169,207,236,288,336,361,512,519,596,607,613 (two per line)
core/src/stringlist.cc:1158,1196
core/src/util.cc:241,327,334,737,1414,1461,1571,1585,1644,1645,1650,1652,1711 (two per line),1718,1719,1727,1747,1748,1795,1796,1837,1848,1849,1862,1868,1905,1920,1935,1950,2029,2030,2226,2229,2315,2382,2446
core/src/view.cc:638,716,717,728,839,871,1693,1941,1942,2300,2484,2485,2837

## `-Wunused-variable` (15)

core/src/file_netcdf.cc:164 (n_groups), 1099 (gn_slash), 1103 (parent_id), 1104 (id1, id2, id3), 1797 (n_vars), 1799 (n_gatts, rec_dim), 2134 (tlen)
core/src/ncview.cc:659 (i)
core/src/util.cc:2332 (i), 2441 (i0, i1), 2442 (ts)

## `-Wunused-but-set-variable` (10)

core/src/do_print.cc:222 (istat)
core/src/file_netcdf.cc:81 (dummyerr), 2120 (ierr), 2133 (ierr), 2148 (ierr)
core/src/udu.cc:134 (rettype)
core/src/util.cc:1404 (sum), 2332 (ierr, n_so_far)
core/src/view.cc:755 (ierr)

## `-Wstringop-truncation` (4)

core/src/view.cc:2214, 2225 (strncpy truncating an 80-byte field from a 1023-byte source)
core/src/view.cc:2970, 2973 (strncat truncating a 100-byte append from a 127-byte source)

## `-Wparentheses` (1)

core/src/file_netcdf.cc:1135 — `suggest parentheses around assignment used as truth value`

## `-Wempty-body` (1)

core/src/view.cc:578 — `suggest braces around empty body in an 'else' statement`

## `-Waddress` (1)

core/src/file_netcdf.cc:493 — `the address of 'groupname' will never be NULL`

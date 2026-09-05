# ncview (C++ / FLTK port)

A from-scratch C++/FLTK/CMake port of [ncview](http://cirrus.ucsd.edu/ncview/),
David W. Pierce's netCDF visual browser. The original is C + X11/Xt + Athena
widgets (Xaw), built with autotools. This tree replaces the toolkit with
[FLTK](https://www.fltk.org/) and the build with CMake, and vendors both FLTK
and [UDUNITS-2](https://www.unidata.ucar.edu/software/udunits/) as git
submodules so the only remaining system dependency is netCDF (plus expat, for
UDUNITS-2's XML parsing).

See `PORTING.md` for the porting plan and status.

## Building

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build
```

Note: the root-level `README` and `COPYRIGHT` files (no `.md`/other suffix)
are *not* this project's docs — they are copies of UDUNITS-2's own files,
kept here only because UDUNITS-2's vendored `CMakeLists.txt` hardcodes
`${CMAKE_SOURCE_DIR}/README` and `${CMAKE_SOURCE_DIR}/COPYRIGHT` for its
CPack metadata, and `CMAKE_SOURCE_DIR` resolves to our repo root once it's
pulled in via `add_subdirectory`.

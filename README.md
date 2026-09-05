# ncview (C++ / FLTK port)

A from-scratch C++/FLTK/CMake port of [ncview](http://cirrus.ucsd.edu/ncview/),
David W. Pierce's netCDF visual browser. The original is C + X11/Xt + Athena
widgets (Xaw), built with autotools. This tree replaces the toolkit with
[FLTK](https://www.fltk.org/) and the build with CMake, and vendors both FLTK
and [UDUNITS-2](https://www.unidata.ucar.edu/software/udunits/) as git
submodules so the only remaining system dependency is netCDF (plus expat, for
UDUNITS-2's XML parsing).

See `PORTING.md` for the porting plan and status.

## Prerequisites

- A C++17 compiler and CMake >= 3.20.
- netCDF (the C library + headers; found via `find_package(netCDF)` or,
  failing that, pkg-config).
- expat (`libexpat-dev`/`expat-devel`), required by the vendored UDUNITS-2 build.
- X11 development headers, required by FLTK's Linux/BSD backend (e.g.
  Debian/Ubuntu: `libx11-dev libxext-dev libxft-dev libxinerama-dev
  libxcursor-dev libxrender-dev libxfixes-dev`; other distros' package names
  vary, see FLTK's own docs).

FLTK and UDUNITS-2 themselves need nothing pre-installed: both are built from
the vendored `third_party/` submodules, statically, as part of this build.

## Building

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build
```

This produces `build/app/ncview`, runnable in place:

```sh
./build/app/ncview some_file.nc
```

## Installing

```sh
cmake --install build --prefix /usr/local
```

installs the `ncview` binary, the man page, and a handful of supplementary
`*.ncmap` colormap files (the full built-in colormap set ships compiled into
the binary regardless -- these are just the extra, less common ones upstream
distributes as standalone files). `cmake --build build --target package`
(via the CPack config in the top-level `CMakeLists.txt`) produces a `.tar.gz`
instead.

Note: because of a quirk in the vendored UDUNITS-2 build (see below), the
*default* install prefix if you don't pass `--prefix` resolves to this
repo's parent directory, not `/usr/local` -- always pass `--prefix`
explicitly for a real install.

Note: the root-level `README`, `COPYRIGHT`, and `CHANGE_LOG` files (no
`.md`/other suffix) are *not* this project's docs — they are copies of
UDUNITS-2's own files, kept here only because UDUNITS-2's vendored
`CMakeLists.txt` hardcodes `${CMAKE_SOURCE_DIR}/README`,
`${CMAKE_SOURCE_DIR}/COPYRIGHT`, and `${CMAKE_SOURCE_DIR}/CHANGE_LOG` for
its install/CPack rules, and `CMAKE_SOURCE_DIR` resolves to our repo root
once it's pulled in via `add_subdirectory`.

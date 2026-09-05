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

- A C++17 compiler and CMake >= 3.21 (the install rules use
  `install(RUNTIME_DEPENDENCY_SET ...)`, added in 3.21).
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

## Static linking (for HPC clusters etc.)

By default everything outside the vendored `third_party/` submodules (netCDF,
X11, expat, ...) links dynamically, same as any normal build. Pass
`-DNCVIEW_STATIC_LINK=ON` to instead:

- statically link netCDF and its numeric/storage chain (HDF5, zstd, bz2, sz,
  zlib) plus the C++ runtime (`-static-libgcc -static-libstdc++`), so the
  binary doesn't depend on whatever (likely mismatched-version) copies of
  those a given HPC node's module system provides;
- disable FLTK's Wayland backend, which this project never uses at runtime
  anyway (it always forces `FLTK_BACKEND=x11`) but which otherwise drags in
  ~80 transitive shared libraries (GTK, Mesa, EGL, D-Bus, at-spi, LLVM) for
  no benefit.

Requires static (`.a`) builds of netCDF/HDF5/zstd/bz2/sz/zlib to be
findable (configure fails with a clear message naming whichever one isn't);
on Debian/Ubuntu that's typically already covered by the normal `-dev`
packages, on other distros/Homebrew you may need a `-static` package.
`libstdc++.a` specifically needs your distro's static-libstdc++ package
(e.g. Fedora/RHEL's `libstdc++-static`, not installed by default).

Deliberately left dynamic even with this on: curl and libxml2 (netCDF's
optional OPeNDAP/remote-file dependency chain, unused for local files and
pulling in OpenSSL/Kerberos/LDAP -- both unnecessary here and inadvisable to
freeze via static linking) and the X11/Xft stack (stable, always-present
OS-level ABI on any cluster reachable via `ssh -X`).

```sh
cmake -S . -B build-static -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNCVIEW_STATIC_LINK=ON
cmake --build build-static -j
ldd build-static/app/ncview   # confirm netcdf/hdf5 are no longer listed
```

## Installing

```sh
cmake --install build --prefix /usr/local
```

installs the `ncview` binary, the man page, a handful of supplementary
`*.ncmap` colormap files (the full built-in colormap set ships compiled into
the binary regardless -- these are just the extra, less common ones upstream
distributes as standalone files), and every non-system shared library the
binary actually needs at runtime (netCDF, HDF5, X11, curl, ...), copied
alongside it into `lib/` (`lib64/` on some distros) with an rpath pointing
back at that directory -- the install (and the package `cmake --build build
--target package` produces, a `.tar.gz` on Linux/macOS or `.zip` on Windows)
is self-contained and doesn't require any of that separately installed on
the machine it's copied to. Deliberately excluded from bundling: the OS's
own core runtime (libc, the dynamic loader, kernel/GPU-driver-tied libraries
on Linux, Windows' universal-CRT split DLLs) -- bundling those would be
actively harmful (ABI/driver mismatches against the host), not helpful.

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

## Releases

Pre-built, self-contained packages for Linux, macOS, and Windows are
published to [GitHub Releases](../../releases) by
`.github/workflows/release.yml`. To cut a release, tag a commit with a
version matching `v*.*.*` and push the tag:

```sh
git tag v2.1.11
git push origin v2.1.11
```

This triggers the same build/test/package steps CI already runs (shared via
`.github/workflows/build.yml`) on all three platforms, then uploads the
resulting archives to a release named after the tag. Keep the tag in sync
with `CPACK_PACKAGE_VERSION` in the top-level `CMakeLists.txt`, which is the
actual source of truth for the version baked into the packages.

To re-publish the same release (e.g. after a CI infra flake on one
platform) without pushing a new tag, run the workflow manually from the
Actions tab (`workflow_dispatch`) with the existing tag name -- it uploads
over the existing release's assets rather than failing.

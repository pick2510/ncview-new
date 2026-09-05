cmake_minimum_required(VERSION 3.21)  # install(RUNTIME_DEPENDENCY_SET ...) below needs 3.21+
project(ncview LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

# tests/ whole-archive-links ncview_core (and transitively libudunits2) into
# a shared library to force full symbol resolution (see tests/CMakeLists.txt);
# that requires every static lib involved to be PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NCVIEW_BUILD_TESTS "Build ncview_core headless tests" ON)

# HPC clusters routinely lack (or have mismatched-version) shared libraries
# for anything outside their own module system -- this pulls netCDF/HDF5 and
# the C++ runtime in statically so the binary doesn't touch either at
# runtime, and disables FLTK's Wayland backend, which this project never
# uses at runtime anyway (main.cc/README always run with FLTK_BACKEND=x11)
# but which otherwise drags in ~80 transitive shared libraries (GTK, Mesa,
# EGL, D-Bus, at-spi, LLVM...) purely to sit unused. Deliberately NOT static:
# curl/xml2 (netCDF's optional, unused-for-local-files OPeNDAP/remote-access
# dependency, pulling in OpenSSL/Kerberos/LDAP -- both unnecessary here and
# inadvisable to freeze via static linking) and the X11/Xft stack (stable,
# always-present OS-level ABI on any cluster reachable via `ssh -X`).
option(NCVIEW_STATIC_LINK
    "Statically link netCDF/HDF5 and the C++ runtime, and disable FLTK's unused Wayland backend, for portability to systems (e.g. HPC clusters) without matching shared libraries"
    OFF)

if(NCVIEW_STATIC_LINK)
    set(FLTK_BACKEND_WAYLAND OFF CACHE BOOL "" FORCE)
endif()

# --------------------------------------------------------------------------
# netCDF (system dependency)
# --------------------------------------------------------------------------
find_package(netCDF QUIET)
if(NOT netCDF_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NETCDF REQUIRED IMPORTED_TARGET netcdf)
    add_library(netCDF::netcdf ALIAS PkgConfig::NETCDF)
endif()

if(NCVIEW_STATIC_LINK)
    # netCDF's own numeric/storage chain -- swap netCDF::netcdf's dynamic
    # .so for its .a, plus the .a of everything it in turn needs (found the
    # same way autotools/CMake would: by name, via the normal library search
    # path), so the ncview_core -> netCDF::netcdf PUBLIC link (core/CMakeLists.txt)
    # picks it up transitively with no changes needed there.
    # netCDF_DIR (set by find_package(netCDF)'s own CMake config, e.g.
    # <prefix>/lib/cmake/netCDF) tells us which prefix's lib/ directory to
    # also search -- covers non-system installs (Homebrew/Linuxbrew, a
    # module-system path, spack, ...) without hardcoding one.
    set(_ncview_static_lib_hints)
    if(netCDF_DIR)
        get_filename_component(_ncview_static_lib_hints "${netCDF_DIR}/../.." ABSOLUTE)
    endif()

    set(_ncview_static_netcdf_chain netcdf hdf5_hl hdf5 sz zstd bz2 z)
    set(_ncview_static_netcdf_libs "")
    foreach(_lib ${_ncview_static_netcdf_chain})
        find_library(_ncview_static_${_lib} NAMES lib${_lib}.a HINTS ${_ncview_static_lib_hints})
        if(NOT _ncview_static_${_lib})
            message(FATAL_ERROR
                "NCVIEW_STATIC_LINK is ON but lib${_lib}.a was not found. "
                "Install a static build of ${_lib} (e.g. via your package "
                "manager's -static/-devel package) or turn NCVIEW_STATIC_LINK off.")
        endif()
        list(APPEND _ncview_static_netcdf_libs "${_ncview_static_${_lib}}")
    endforeach()
    list(POP_FRONT _ncview_static_netcdf_libs _ncview_static_netcdf_a)
    # netCDF's own exported target declares per-configuration locations
    # (IMPORTED_LOCATION_RELEASE, etc. -- see IMPORTED_CONFIGURATIONS) that
    # CMake consults *before* the plain IMPORTED_LOCATION, so overriding
    # only the latter silently has no effect on the actual link line;
    # every configuration CMake might map our build type to needs the
    # override too.
    set_target_properties(netCDF::netcdf PROPERTIES
        IMPORTED_LOCATION "${_ncview_static_netcdf_a}"
        IMPORTED_LOCATION_RELEASE "${_ncview_static_netcdf_a}"
        IMPORTED_LOCATION_DEBUG "${_ncview_static_netcdf_a}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${_ncview_static_netcdf_a}"
        IMPORTED_LOCATION_MINSIZEREL "${_ncview_static_netcdf_a}"
        IMPORTED_LOCATION_NOCONFIG "${_ncview_static_netcdf_a}"
        INTERFACE_LINK_LIBRARIES "${_ncview_static_netcdf_libs};curl;xml2;m"
    )
endif()

# M0's plan called for an NCVIEW_ENABLE_PNG option guarding a system libpng
# dependency for the "-frames" PNG dump. M5 implemented that dump with
# FLTK's own fl_write_png instead (ui/src/interface_fltk.cc) -- FLTK already
# bundles libpng for its own image support, so PNG output is unconditional
# and there is no separate libpng dependency to gate.

# --------------------------------------------------------------------------
# Vendored third-party libraries (submodules)
# --------------------------------------------------------------------------
set(FLTK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLUID OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FORMS OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/fltk)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# Upstream's CMakeLists.txt assumes it is the top-level project: its CPack
# block and its `install(FILES "${CMAKE_SOURCE_DIR}/COPYRIGHT" ...)` resolve
# CMAKE_SOURCE_DIR to *our* repo root when add_subdirectory'd. We keep root
# -level README/COPYRIGHT files (copies of udunits2's own) purely so those
# hardcoded paths resolve; our actual project readme is README.md.
add_subdirectory(third_party/udunits2)

# --------------------------------------------------------------------------
# ncview
# --------------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(app)

if(NCVIEW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --------------------------------------------------------------------------
# Install rules
# --------------------------------------------------------------------------
install(TARGETS ncview
    RUNTIME_DEPENDENCY_SET ncview_runtime_deps
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY data/colormaps/ DESTINATION ${CMAKE_INSTALL_DATADIR}/ncview/colormaps)
install(FILES data/ncview.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

# Bundle every non-system shared library ncview actually needs at runtime
# (netCDF, HDF5, X11, curl, ...) next to the binary, so a packaged archive
# is self-contained and doesn't require any of that separately installed on
# the machine that unpacks it. Deliberately excludes the OS's own core
# runtime (libc, the dynamic loader, kernel/GPU-driver-tied libraries on
# Linux, Windows' universal-CRT split DLLs) -- bundling those would be
# actively harmful (ABI/driver mismatches against the host), not helpful.
if(WIN32)
    # DLLs are searched for next to the .exe -- no rpath mechanism to set up.
    set(_ncview_bundle_dir ${CMAKE_INSTALL_BINDIR})
    set(_ncview_bundle_pre_exclude "api-ms-.*" "ext-ms-.*")
    set(_ncview_bundle_post_exclude ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss]y[Ss]tem32.*")
elseif(APPLE)
    set(_ncview_bundle_dir ${CMAKE_INSTALL_LIBDIR})
    set(_ncview_bundle_pre_exclude)
    set(_ncview_bundle_post_exclude "^/usr/lib/.*" "^/System/Library/.*")
else()
    set(_ncview_bundle_dir ${CMAKE_INSTALL_LIBDIR})
    set(_ncview_bundle_pre_exclude)
    set(_ncview_bundle_post_exclude
        "^linux-vdso.*"
        "^/lib/ld-linux.*" "^/lib64/ld-linux.*" "^/lib/x86_64-linux-gnu/ld-.*"
        ".*/libc\\.so.*" ".*/libm\\.so.*" ".*/libdl\\.so.*"
        ".*/libpthread\\.so.*" ".*/librt\\.so.*" ".*/libresolv\\.so.*"
        ".*/libGL\\.so.*" ".*/libEGL\\.so.*" ".*/libgbm\\.so.*" ".*/libdrm.*"
        ".*/dri/.*"
    )
endif()

install(RUNTIME_DEPENDENCY_SET ncview_runtime_deps
    DESTINATION ${_ncview_bundle_dir}
    PRE_EXCLUDE_REGEXES ${_ncview_bundle_pre_exclude}
    POST_EXCLUDE_REGEXES ${_ncview_bundle_post_exclude}
)

if(UNIX AND NOT APPLE)
    set_target_properties(ncview PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
        BUILD_WITH_INSTALL_RPATH FALSE
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
elseif(APPLE)
    set_target_properties(ncview PROPERTIES
        INSTALL_RPATH "@executable_path/../${CMAKE_INSTALL_LIBDIR}"
        BUILD_WITH_INSTALL_RPATH FALSE
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
endif()

# --------------------------------------------------------------------------
# CPack (source/binary tarball)
# --------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "ncview")
set(CPACK_PACKAGE_VERSION "2.1.11")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A visual netCDF file browser (C++/FLTK port)")
# ZIP on Windows (no tar/gzip on a bare runner, and it's the platform-native
# archive format there); TGZ everywhere else.
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_SOURCE_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_SOURCE_GENERATOR "TGZ")
endif()
set(CPACK_SOURCE_IGNORE_FILES "/build/;/build-.*/;/\\\\.git/")
include(CPack)

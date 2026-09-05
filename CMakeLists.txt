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

# UDUNITS-2's own CMakeLists.txt (lib/CMakeLists.txt) only regenerates
# parser.c/scanner.c from their bison/flex sources "IF(UNIX)" -- on Windows
# (MinGW) that's false regardless of whether flex/bison are installed, so
# its add_library() a few lines later fails outright with "Cannot find
# source file: parser.c" (neither generated file is committed to the
# submodule, only their .y/.l sources are). Generate both ourselves,
# unconditionally, so they simply already exist by the time UDUNITS-2's own
# CMakeLists.txt looks for them -- fixes Windows without touching the
# vendored submodule, and is a no-op on Unix where its own custom command
# would just regenerate the same files anyway.
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)
set(_udunits2_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/udunits2/lib")
if(NOT EXISTS "${_udunits2_lib_dir}/scanner.c")
    execute_process(
        COMMAND "${FLEX_EXECUTABLE}" -d -Put -o scanner.c scanner.l
        WORKING_DIRECTORY "${_udunits2_lib_dir}"
        RESULT_VARIABLE _flex_result
    )
    if(NOT _flex_result EQUAL 0)
        message(FATAL_ERROR "Failed to generate UDUNITS-2's scanner.c via flex")
    endif()
endif()
if(NOT EXISTS "${_udunits2_lib_dir}/parser.c")
    execute_process(
        COMMAND "${BISON_EXECUTABLE}" -t -p ut -o parser.c parser.y
        WORKING_DIRECTORY "${_udunits2_lib_dir}"
        RESULT_VARIABLE _bison_result
    )
    if(NOT _bison_result EQUAL 0)
        message(FATAL_ERROR "Failed to generate UDUNITS-2's parser.c via bison")
    endif()
endif()

if(WIN32)
    # UDUNITS-2's texi_doc() (called three times: its own CMakeLists.txt,
    # lib/, and prog/) unconditionally depends on generated .info/.html
    # files once a makeinfo is found (the same "ALL target depends on a
    # file nothing generates" shape as the flex/bison fix above). On
    # Windows, MSYS2's `texinfo` package's makeinfo is a Perl script under
    # its POSIX layer (/usr/bin), not a native .exe -- ninja (itself a
    # native process, invoked outside the MSYS2 shell) can't run it at all
    # ("is not recognized as an internal or external command").
    #
    # ncview has no use for UDUNITS-2's own documentation, so rather than
    # chase down a native makeinfo: the CI workflow simply doesn't install
    # the `texinfo` MSYS2 package on Windows, so find_program(MAKEINFO
    # makeinfo) genuinely finds nothing and texi_doc() takes its own "not
    # found" branch, never defining a command to (re)generate these files
    # at all. (Pre-setting the MAKEINFO cache variable to a NOTFOUND-
    # suffixed value here does *not* work as a substitute for that -- CMake
    # specifically treats an existing "<var>-NOTFOUND" cache value as "not
    # found *yet*, keep searching" and overwrites it with a fresh, real
    # result if one exists on PATH, confirmed locally.) What's still needed
    # regardless of *how* it ends up not found: pre-create empty
    # placeholders at the exact paths its add_custom_target(... ALL
    # DEPENDS ...) unconditionally references, so each is a plain file
    # with no producer rule -- permanently satisfied, not a missing target.
    foreach(_doc_output
        third_party/udunits2/udunits2.info
        third_party/udunits2/udunits2.html
        third_party/udunits2/lib/udunits2lib.info
        third_party/udunits2/lib/udunits2lib.html
        third_party/udunits2/prog/udunits2prog.info
        third_party/udunits2/prog/udunits2prog.html
    )
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_doc_output}")
            file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/${_doc_output}" "")
        endif()
    endforeach()
endif()

# Upstream's CMakeLists.txt assumes it is the top-level project: its CPack
# block and its `install(FILES "${CMAKE_SOURCE_DIR}/COPYRIGHT" ...)` resolve
# CMAKE_SOURCE_DIR to *our* repo root when add_subdirectory'd. We keep root
# -level README/COPYRIGHT files (copies of udunits2's own) purely so those
# hardcoded paths resolve; our actual project readme is README.md.
add_subdirectory(third_party/udunits2)

# UDUNITS-2's own CMakeLists.txt only escapes backslashes in the path it
# bakes into config.h's DEFAULT_UDUNITS2_XML_PATH "IF(MSVC)" -- on Windows/
# MinGW (CMAKE_CXX_COMPILER_ID GNU, not MSVC, which is how this project
# builds on Windows -- see the flex/bison note above and the CI workflow)
# that guard is false, leaving raw Windows backslashes sitting in a C
# string literal, which the compiler then tries to parse as escape
# sequences and fails outright ("incomplete universal character name
# \u..."). Windows' own path APIs (and ut_read_xml()) accept forward
# slashes identically to backslashes, so normalize the generated header
# ourselves after the fact rather than patch the vendored submodule.
if(WIN32)
    set(_udunits2_config_h "${CMAKE_BINARY_DIR}/third_party/udunits2/config.h")
    if(EXISTS "${_udunits2_config_h}")
        file(READ "${_udunits2_config_h}" _udunits2_config_h_content)
        string(REPLACE "\\" "/" _udunits2_config_h_content "${_udunits2_config_h_content}")
        file(WRITE "${_udunits2_config_h}" "${_udunits2_config_h_content}")
    endif()
endif()

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
    # install(RUNTIME_DEPENDENCY_SET)'s resolver finds *names* of needed
    # DLLs (libstdc++-6.dll, libnetcdf.dll, ...) via objdump fine on its
    # own, but doesn't know where any of them actually live on disk unless
    # told -- MinGW/MSYS2 keeps every package's DLLs (the compiler's own
    # runtime included) in one shared bin/ next to the compiler itself, so
    # deriving that directory from CMAKE_CXX_COMPILER covers all of them
    # without hardcoding a path.
    get_filename_component(_ncview_mingw_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_ncview_bundle_search_dirs "${_ncview_mingw_bin_dir}")
elseif(APPLE)
    set(_ncview_bundle_dir ${CMAKE_INSTALL_LIBDIR})
    set(_ncview_bundle_pre_exclude)
    set(_ncview_bundle_post_exclude "^/usr/lib/.*" "^/System/Library/.*")
    set(_ncview_bundle_search_dirs)
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
    set(_ncview_bundle_search_dirs)
endif()

install(RUNTIME_DEPENDENCY_SET ncview_runtime_deps
    DESTINATION ${_ncview_bundle_dir}
    PRE_EXCLUDE_REGEXES ${_ncview_bundle_pre_exclude}
    POST_EXCLUDE_REGEXES ${_ncview_bundle_post_exclude}
    DIRECTORIES ${_ncview_bundle_search_dirs}
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

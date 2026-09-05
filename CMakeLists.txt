cmake_minimum_required(VERSION 3.20)
project(ncview LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# tests/ whole-archive-links ncview_core (and transitively libudunits2) into
# a shared library to force full symbol resolution (see tests/CMakeLists.txt);
# that requires every static lib involved to be PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NCVIEW_ENABLE_PNG "Enable PNG frame dump support" ON)
option(NCVIEW_BUILD_TESTS "Build ncview_core headless tests" ON)

# --------------------------------------------------------------------------
# netCDF (system dependency)
# --------------------------------------------------------------------------
find_package(netCDF QUIET)
if(NOT netCDF_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NETCDF REQUIRED IMPORTED_TARGET netcdf)
    add_library(netCDF::netcdf ALIAS PkgConfig::NETCDF)
endif()

if(NCVIEW_ENABLE_PNG)
    find_package(PNG)
    if(NOT PNG_FOUND)
        message(WARNING "libpng not found; disabling NCVIEW_ENABLE_PNG")
        set(NCVIEW_ENABLE_PNG OFF)
    endif()
endif()

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

install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_PREFIX}/share/ncview)

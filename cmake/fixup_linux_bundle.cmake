# Run at install time (via install(SCRIPT ...) in the top-level
# CMakeLists.txt), after install(RUNTIME_DEPENDENCY_SET) has copied every
# bundled shared library into lib/ alongside the ncview binary in bin/.
#
# ncview itself gets an INSTALL_RPATH of $ORIGIN/../lib (set in the
# top-level CMakeLists.txt), which correctly resolves its own *direct*
# NEEDED entries (libnetcdf.so, libX11.so, ...) against the bundle. But
# ELF's RUNPATH (unlike the older, deprecated RPATH) is NOT transitive: it
# only governs the direct NEEDED lookups of the object that actually
# carries it. None of the bundled libraries themselves were relinked by
# us -- they're copied as-is from wherever the CI runner's package manager
# put them -- so they carry whatever RPATH (usually none) they originally
# had, meaning THEIR OWN dependencies (e.g. libnetcdf.so needing
# libhdf5.so, which in turn needs libcurl.so) are never looked up in the
# bundle at all, even though the files sit right next to each other in
# the same lib/ directory. Confirmed with a real built package: `ldd`
# reports libhdf5_serial_hl.so.100/libhdf5_serial.so.103/
# libcurl-gnutls.so.4 as "not found" despite being physically present.
#
# Fix: give every bundled library its own rpath back to its own
# directory ($ORIGIN, i.e. lib/ itself) via patchelf, so the whole
# dependency chain resolves against the bundle regardless of how many
# levels deep it goes, on a machine with none of this installed.

find_program(NCVIEW_PATCHELF patchelf)
if(NOT NCVIEW_PATCHELF)
    message(FATAL_ERROR "patchelf is required to fix up the bundled package's rpaths but was not found")
endif()

set(_ncview_lib_dir "${CMAKE_INSTALL_PREFIX}/${NCVIEW_BUNDLE_LIBDIR}")
file(GLOB _ncview_bundled_libs "${_ncview_lib_dir}/*.so*")

foreach(_ncview_lib ${_ncview_bundled_libs})
    if(NOT IS_SYMLINK "${_ncview_lib}")
        execute_process(
            COMMAND "${NCVIEW_PATCHELF}" --set-rpath "$ORIGIN" "${_ncview_lib}"
            RESULT_VARIABLE _ncview_patchelf_result
        )
        if(NOT _ncview_patchelf_result EQUAL 0)
            message(FATAL_ERROR "patchelf --set-rpath failed on ${_ncview_lib}")
        endif()
    endif()
endforeach()

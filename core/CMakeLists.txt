# M1: core/*.cc is upstream src/*.c, mechanically ported to compile as C++.
# No UI symbols reach this library; it talks to ncview_ui only through the
# ncview/interface.h seam. See PORTING.md.
add_library(ncview_core STATIC
    src/calcalcs.cc
    src/do_buttons.cc
    src/do_print.cc
    src/epic_time.cc
    src/file.cc
    src/file_netcdf.cc
    src/handle_rc_file.cc
    src/interface_glue.cc
    src/ncview.cc
    src/overlay.cc
    src/stringlist.cc
    src/udu.cc
    src/utCalendar2_cal.cc
    src/util.cc
    src/view.cc
)
set_target_properties(ncview_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(ncview_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(ncview_core PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/udunits2/lib
)
target_link_libraries(ncview_core PUBLIC
    netCDF::netcdf
    libudunits2
)

# Upstream's linked lists type their next/prev pointers as `void *` and rely
# on C's implicit void*->T* conversion at every traversal site; there are
# hundreds of these across core/. -fpermissive downgrades that (and a few
# similar C-isms) from a hard error to a warning under GCC, which is the
# pragmatic choice for a mechanical C->C++ port versus casting every site.
# TODO(M1 follow-up): replace void* next/prev with real self-referential
# pointer types and drop this flag.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ncview_core PRIVATE -fpermissive)
endif()

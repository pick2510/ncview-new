# M3: the FLTK implementation of the ncview/interface.h toolkit seam.
add_library(ncview_ui STATIC
    src/interface_fltk.cc
    src/main_window.cc
    src/plot_window.cc
)
target_include_directories(ncview_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(ncview_ui PUBLIC
    ncview_core
    fltk
    fltk_images
)
target_link_libraries(ncview_ui PRIVATE ncview_warning_flags)

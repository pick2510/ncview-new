add_executable(ncview
    main.cc
)
# ncview_ui and ncview_core call into each other (core calls the in_*
# contract ncview_ui implements; ncview_ui calls core functions like
# view_report_position directly) -- a genuine circular static-lib
# dependency, so a plain link line only resolves it left-to-right once.
# --start-group/--end-group makes the linker keep re-scanning both archives
# until nothing new gets pulled in.
target_link_libraries(ncview PRIVATE
    -Wl,--start-group ncview_ui ncview_core -Wl,--end-group
)

if(NCVIEW_STATIC_LINK)
    # The single most common reason a binary built on one machine won't run
    # on an HPC compute/login node: a GCC/libstdc++ version older than the
    # one it was built with. Freezing our own copy of the runtime in avoids
    # that regardless of what's on the target node.
    target_link_options(ncview PRIVATE -static-libgcc -static-libstdc++)
endif()

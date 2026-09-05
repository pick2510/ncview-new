add_executable(ncview
    main.cc
)
# ncview_ui and ncview_core call into each other (core calls the in_*
# contract ncview_ui implements; ncview_ui calls core functions like
# view_report_position directly) -- a genuine circular static-lib
# dependency, so a plain link line only resolves it left-to-right once.
# GNU ld/LLD's -Wl,--start-group/--end-group would make the linker keep
# re-scanning both archives until nothing new gets pulled in, but Apple's
# ld64 doesn't understand that syntax at all ("unknown options:
# --whole-archive --no-whole-archive" -- ld64 has no direct equivalent).
# Simply listing both archives twice is the old, universally-portable
# trick for a two-library cycle: every linker (GNU ld, LLD, ld64,
# MSVC link.exe) rescans a static archive each time it's named on the
# link line, so by the second pass anything either side still needed
# from the other has already been seen once and gets resolved.
target_link_libraries(ncview PRIVATE
    ncview_ui ncview_core ncview_ui ncview_core
    ncview_warning_flags
)

if(NCVIEW_STATIC_LINK)
    # The single most common reason a binary built on one machine won't run
    # on an HPC compute/login node: a GCC/libstdc++ version older than the
    # one it was built with. Freezing our own copy of the runtime in avoids
    # that regardless of what's on the target node.
    target_link_options(ncview PRIVATE -static-libgcc -static-libstdc++)
endif()

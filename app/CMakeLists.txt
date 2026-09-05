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

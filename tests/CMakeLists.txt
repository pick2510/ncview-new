# M1 checkpoint, part 1: prove every symbol ncview_core needs is resolved by
# nothing but the headless stub_interface.cc implementation of
# ncview/interface.h (no UI, no X11). A shared library has no `main`, so
# whole-archive-linking ncview_core.a into it (pulling in every .o,
# including ncview.cc's real main()) can't collide with anything -- any
# missing symbol anywhere in ncview_core fails right here at link time.
add_library(ncview_core_linkcheck SHARED
    stub_interface.cc
)
target_link_libraries(ncview_core_linkcheck PRIVATE
    -Wl,--whole-archive ncview_core -Wl,--no-whole-archive
)

# M1 checkpoint, part 2: functional behavior of a sample of pure,
# global-state-free core logic. Links normally (not whole-archive), so only
# the .o's this test actually calls get pulled in -- no main() clash.
add_executable(core_headless_test
    core_headless_test.cc
    stub_interface.cc
)
target_link_libraries(core_headless_test PRIVATE ncview_core)
add_dependencies(core_headless_test ncview_core_linkcheck)
add_test(NAME core_headless_test COMMAND core_headless_test)

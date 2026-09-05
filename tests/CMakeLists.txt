# M1 checkpoint, part 1: prove every symbol ncview_core needs is resolved by
# nothing but the headless stub_interface.cc implementation of
# ncview/interface.h (no UI, no X11). ncview_core has no `main` of its own
# (M1 renamed upstream's to ncview_main()), so whole-archive-linking it
# into an ordinary executable (pulling in every .o) can't collide with
# anything -- any missing symbol anywhere in ncview_core fails right here
# at link time. Deliberately an executable, not a shared library: a shared
# object requires every static archive linked into it to be PIC, which
# NCVIEW_STATIC_LINK's non-PIC system static libs (e.g. Homebrew's
# libz.a) aren't -- an executable carries no such requirement.
add_executable(ncview_core_linkcheck
    stub_interface.cc
    linkcheck_main.cc
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

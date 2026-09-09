// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for g_app.controller.draw() (Phase 2: becomes
// ViewerController::draw()) -- framestore caching on/off, forced range
// recompute, and autoscale. Written against the unmodified free function
// first, per this plan's characterization rule.
//
// Not covered here: the lockout_view_changes re-entrancy guard. Exercising
// it for real means getting a nested draw() call from inside a UI
// callback draw() itself triggers (e.g. a modal dialog popped from
// data_to_pixels()'s "min and max both 0" path) -- RecordingViewerUi's
// stubs don't currently re-enter core that way, and building that
// wiring is its own piece of work, not a Phase 2 side quest. Left for a
// future pass (noted in PORTING.md's Phase 2 entry).
#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/nc_fixture.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::Constant;
using ncview_test::NcFixture;
using ncview_test::SessionFixture;

// The global under test, defined in core/src/view.cc.
extern std::unique_ptr<ViewState> &view;
extern FrameCache &framestore;

namespace {

void select_draw_variable(NcFixture &nc, const char *var_name, int nt) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    nc.dim("time", nt).dim("lat", 2).dim("lon", 2)
      .timeAxis("time", "days since 2000-01-01")
      .coord("lat").coord("lon")
      .var(var_name, {"time", "lat", "lon"});
    int fid = nc.openForCore();
    g_dataset.addVariable(var_name, fid, nc.path().c_str());
    in_variable_selected(var_name);
}

} // namespace

TEST_CASE("view_draw: framestore stays invalid when save_frames is off") {
    SessionFixture fx;
    NcFixture nc;
    // ensure_ncview_misc_initialized() runs the real initialize_misc()
    // exactly once per process, and initialize_misc() itself calls
    // reset_session_defaults() -- which sets options.save_frames back to
    // DEFAULT_SAVEFRAMES (true). Call it *before* overriding save_frames
    // below, not after: whichever test in the suite happens to run first
    // triggers that one-time call, and setting save_frames=false first
    // would silently be undone by it if select_draw_variable() (which
    // also calls ensure_ncview_misc_initialized()) ran first instead --
    // exactly the kind of order-dependence a shuffled-order run exists to
    // catch.
    ensure_ncview_misc_initialized();
    options.save_frames = false;
    select_draw_variable(nc, "draw_no_framestore", 3);

    CHECK_FALSE(framestore.valid());
    CHECK(g_app.controller.draw(true, false) == 0);
    CHECK_FALSE(framestore.valid());
}

TEST_CASE("view_draw: framestore caches a drawn frame when save_frames is on") {
    SessionFixture fx;
    NcFixture nc;
    options.save_frames = true;
    select_draw_variable(nc, "draw_framestore", 3);
    REQUIRE(framestore.valid());

    CHECK(g_app.controller.draw(true, false) == 0);
    CHECK(framestore.lookup(0) != nullptr);

    options.save_frames = false;
}

TEST_CASE("view_draw: force_range_to_frame recomputes user_min/user_max from the current frame") {
    SessionFixture fx;
    NcFixture nc;
    select_draw_variable(nc, "draw_forced_range", 3);
    REQUIRE(view != nullptr);

    view->variable->user_min = -999;
    view->variable->user_max = 999;
    // allow_framestore_usage=false: save_frames defaults to true (see
    // ncview.cc's DEFAULT_SAVEFRAMES), so the frame selection just drew is
    // already cached -- with allow_framestore_usage=true, view_draw's own
    // framestore-hit path returns early *before* the force_range_to_frame
    // recompute ever runs, an interaction only visible by actually running
    // this rather than reading the code.
    CHECK(g_app.controller.draw(false, true) == 0);
    // The synthetic Ramp variable is nonconstant per frame, so forcing a
    // recompute must move the range away from the placeholder values.
    CHECK(view->variable->user_min != -999);
    CHECK(view->variable->user_max != 999);
}

TEST_CASE("view_draw: autoscale recomputes the range on every draw, even without force_range_to_frame") {
    SessionFixture fx;
    NcFixture nc;
    select_draw_variable(nc, "draw_autoscale", 3);
    REQUIRE(view != nullptr);
    options.autoscale = true;

    view->variable->user_min = -999;
    view->variable->user_max = 999;
    CHECK(g_app.controller.draw(true, false) == 0);
    CHECK(view->variable->user_min != -999);
    CHECK(view->variable->user_max != 999);

    options.autoscale = false;
}

TEST_CASE("view_draw: a constant-valued variable still draws without crashing") {
    SessionFixture fx;
    NcFixture nc;
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    nc.dim("time", 2).dim("lat", 2).dim("lon", 2)
      .timeAxis("time", "days since 2000-01-01")
      .coord("lat").coord("lon")
      .var("draw_constant", {"time", "lat", "lon"}, Constant{5.0f});
    int fid = nc.openForCore();
    g_dataset.addVariable("draw_constant", fid, nc.path().c_str());
    in_variable_selected("draw_constant");
    REQUIRE(view != nullptr);

    // Must not crash even though every value (and so global_min ==
    // global_max) is identical -- view_draw's own degenerate-range path.
    g_app.controller.draw(true, false);
}

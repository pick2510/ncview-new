// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for g_app.controller.changeCurDim()/g_app.controller.setCurDimIndex()/
// g_app.session.curDimIndex() (Phase 2: ViewerController::changeCurDim()/
// setCurDimIndex(), ViewerSession::curDimIndex()) -- round-trip, clamping
// at both ends, and rejecting an attempt to move the X or Y axis. Written
// against the unmodified free functions first, per this plan's
// characterization rule.
#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/nc_fixture.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::NcFixture;
using ncview_test::SessionFixture;

// The global under test, defined in core/src/view.cc.
extern std::unique_ptr<ViewState> &view;

namespace {

// A (level, lat, lon) variable -- "level" is a plain, non-scan dimension
// that g_app.controller.changeCurDim()/g_app.controller.setCurDimIndex() can step, since the
// X/Y axes default to the last two dims (lat, lon).
void select_dims_variable(NcFixture &nc, const char *var_name, int nlevel) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    nc.dim("level", nlevel).dim("lat", 2).dim("lon", 2)
      .coord("level").coord("lat").coord("lon")
      .var(var_name, {"level", "lat", "lon"});
    int fid = nc.openForCore();
    g_dataset.addVariable(var_name, fid, nc.path().c_str());
    in_variable_selected(var_name);
}

} // namespace

TEST_CASE("view_set_cur_dim_index/view_get_cur_dim_index: round-trip") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_roundtrip", 5);

    g_app.controller.setCurDimIndex("level", 3);
    CHECK(g_app.session.curDimIndex("level") == 3);

    g_app.controller.setCurDimIndex("level", 0);
    CHECK(g_app.session.curDimIndex("level") == 0);
}

TEST_CASE("view_set_cur_dim_index: clamps a negative place up to 0") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_clamp_low", 5);

    g_app.controller.setCurDimIndex("level", -3);
    CHECK(g_app.session.curDimIndex("level") == 0);
}

TEST_CASE("view_set_cur_dim_index: clamps a too-large place down to size-1") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_clamp_high", 5);

    g_app.controller.setCurDimIndex("level", 999);
    CHECK(g_app.session.curDimIndex("level") == 4);
}

TEST_CASE("view_change_cur_dim: Modifier::M1 steps forward by one, wrapping at the end") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_step_fwd", 3);
    REQUIRE(g_app.session.curDimIndex("level") == 0);

    char dim_name[] = "level";
    g_app.controller.changeCurDim(dim_name, Modifier::M1);
    CHECK(g_app.session.curDimIndex("level") == 1);
    g_app.controller.changeCurDim(dim_name, Modifier::M1);
    CHECK(g_app.session.curDimIndex("level") == 2);
    g_app.controller.changeCurDim(dim_name, Modifier::M1);
    CHECK(g_app.session.curDimIndex("level") == 0); // wraps
}

TEST_CASE("view_change_cur_dim: Modifier::M3 steps backward by one, wrapping at zero") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_step_bwd", 3);
    REQUIRE(g_app.session.curDimIndex("level") == 0);

    char dim_name[] = "level";
    g_app.controller.changeCurDim(dim_name, Modifier::M3);
    CHECK(g_app.session.curDimIndex("level") == 2); // wraps
    g_app.controller.changeCurDim(dim_name, Modifier::M3);
    CHECK(g_app.session.curDimIndex("level") == 1);
}

TEST_CASE("view_change_cur_dim: refuses to move the Y (or X) axis") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_reject_axis", 3);
    REQUIRE(view != nullptr);
    int y_axis_id = view->y_axis_id;
    size_t before = view->var_place[y_axis_id];

    char dim_name[] = "lat"; // the current Y axis
    g_app.controller.changeCurDim(dim_name, Modifier::M1);
    CHECK(view->var_place[y_axis_id] == before);
}

TEST_CASE("view_get_cur_dim_index: unknown dimension name returns 0") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_unknown", 3);

    CHECK(g_app.session.curDimIndex("no_such_dim") == 0);
}

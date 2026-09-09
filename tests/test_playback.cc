// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for ViewerController's playback actions
// (core/src/viewer_controller.cc: rewind/fastforward/pause/restart),
// using stub_interface.cc's fake one-shot timer queue (added for this
// purpose -- "refine the architecture" plan, Phase 0b). Before that
// queue existed, in_timer_set() silently dropped every callback core
// handed the UI, so none of this had ever actually run under test:
// rewind()/fastforward()'s Modifier::M1 paths only advance the movie
// because their own timer callback re-arms itself and steps the frame
// again, exactly the behavior nothing could previously observe.
#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::SessionFixture;

namespace {

// A (time, lat, lon) variable with enough frames for playback to have
// somewhere to go -- mirrors test_controller_characterization.cc's own
// make_sample_file()/select_fresh_variable() shape.
std::string make_sample_file(const char *var_name, int nt, int nlat, int nlon) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_playback_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_time, dim_lat, dim_lon;
    REQUIRE(nc_def_dim(ncid, "time", nt, &dim_time) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lat", nlat, &dim_lat) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lon", nlon, &dim_lon) == NC_NOERR);

    int var_time, var_lat, var_lon, var_data;
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time) == NC_NOERR);
    std::string units = "days since 2000-01-01";
    REQUIRE(nc_put_att_text(ncid, var_time, "units", units.size(), units.c_str()) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lat", NC_FLOAT, 1, &dim_lat, &var_lat) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lon", NC_FLOAT, 1, &dim_lon, &var_lon) == NC_NOERR);
    int dims[3] = {dim_time, dim_lat, dim_lon};
    REQUIRE(nc_def_var(ncid, var_name, NC_FLOAT, 3, dims, &var_data) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    std::vector<double> tvals(nt);
    for (int i = 0; i < nt; i++) tvals[i] = (double)i;
    REQUIRE(nc_put_var_double(ncid, var_time, tvals.data()) == NC_NOERR);
    std::vector<float> latvals(nlat, 0.0f), lonvals(nlon, 0.0f);
    REQUIRE(nc_put_var_float(ncid, var_lat, latvals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_lon, lonvals.data()) == NC_NOERR);
    std::vector<float> data(nt * nlat * nlon);
    for (size_t i = 0; i < data.size(); i++) data[i] = (float)i;
    REQUIRE(nc_put_var_float(ncid, var_data, data.data()) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

int open_for_core(const std::string &path) {
    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
    return netcdf_fi_initialize(const_cast<char *>(path.c_str()));
}

// Loads and selects a fresh (time, lat, lon) variable with `nt` frames,
// leaving `view` populated and its scan axis on time (axis 0).
std::string select_playback_variable(const char *var_name, int nt) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    std::string path = make_sample_file(var_name, nt, 2, 2);
    int fid = open_for_core(path);
    g_dataset.addVariable(var_name, fid, path.c_str(), 1);
    in_variable_selected(var_name);
    return path;
}

size_t current_frame() {
    REQUIRE(view != nullptr);
    REQUIRE(view->scan_axis_id != -1);
    return view->var_place[view->scan_axis_id];
}

} // namespace

TEST_CASE("playback: rewind arms a timer that steps backward one frame at a time") {
    SessionFixture fx;
    std::string path = select_playback_variable("playback_rewind", 5);
    // Land on a middle frame first so rewind has somewhere to go.
    g_app.controller.restart(Modifier::M1);
    change_view(2, FRAMES);
    REQUIRE(current_frame() == 2);

    g_app.controller.rewind(Modifier::M1);
    CHECK(current_frame() == 1); // change_view(-1, FRAMES) already ran synchronously
    REQUIRE(timerIsArmed());

    REQUIRE(fireTimer());
    CHECK(current_frame() == 0);
    CHECK(timerIsArmed()); // the fired callback re-armed itself

    std::remove(path.c_str());
}

TEST_CASE("playback: fastforward arms a timer that steps forward one frame at a time") {
    SessionFixture fx;
    std::string path = select_playback_variable("playback_fastforward", 5);
    g_app.controller.restart(Modifier::M1);
    REQUIRE(current_frame() == 0);

    g_app.controller.fastforward(Modifier::M1);
    CHECK(current_frame() == 1);
    REQUIRE(timerIsArmed());

    REQUIRE(fireTimer());
    CHECK(current_frame() == 2);
    CHECK(timerIsArmed());

    REQUIRE(fireTimer());
    CHECK(current_frame() == 3);

    std::remove(path.c_str());
}

TEST_CASE("playback: pause clears any pending timer") {
    SessionFixture fx;
    std::string path = select_playback_variable("playback_pause", 5);
    g_app.controller.restart(Modifier::M1);
    g_app.controller.fastforward(Modifier::M1);
    REQUIRE(timerIsArmed());

    g_app.controller.pause(Modifier::M1);
    CHECK_FALSE(timerIsArmed());

    // A stale, already-fired callback must not silently resume playback:
    // there is nothing pending to fire.
    CHECK_FALSE(fireTimer());

    std::remove(path.c_str());
}

TEST_CASE("playback: restart seeks to frame 0 and does not itself arm a timer") {
    SessionFixture fx;
    std::string path = select_playback_variable("playback_restart", 5);
    g_app.controller.restart(Modifier::M1);
    change_view(3, FRAMES);
    REQUIRE(current_frame() == 3);

    g_app.controller.restart(Modifier::M1);
    CHECK(current_frame() == 0);
    // restart() explicitly clears the timer (in_timer_clear()) rather
    // than arming a new one -- the button handlers that follow it
    // (rewind()/fastforward()) are the ones responsible for arming.
    CHECK_FALSE(timerIsArmed());

    std::remove(path.c_str());
}

TEST_CASE("playback: fireTimer() is a documented no-op when nothing is armed") {
    SessionFixture fx;
    CHECK_FALSE(timerIsArmed());
    CHECK_FALSE(fireTimer());
}

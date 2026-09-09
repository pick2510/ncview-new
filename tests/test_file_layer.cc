// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for the fi_*() dispatch layer (core/src/file.cc)
// -- "refine the architecture" plan, Phase 5a. Every existing test that
// touches file I/O opens files via netcdf_fi_initialize() directly and
// calls netcdf_*() functions, reaching straight past the fi_*() layer this
// file exists to pin down. That gap matters because Phase 6 intends to
// collapse fi_*()/netcdf_*() into one layer, and a collapse can't be
// proven safe against code nothing exercises.
//
// Two things this file's own tests had to confirm rather than assume,
// consistent with this plan's "inventory claims are leads, not facts"
// rule:
//  - file.cc's fi_*() forwarders all dispatch on the file-scope static
//    `file_type`, which only ever holds FILE_TYPE_NETCDF (determined by
//    determine_file_type(), the only setter, called via netcdf_fi_confirm()
//    -- see file.cc). Every forwarder's `else` branch is fprintf+exit(-1),
//    so testing the rejection branch here is out of scope: it would kill
//    the test binary, not fail one assertion. determine_file_type() is
//    tested for its accept path only.
//  - Dataset::addVariable()'s `nfiles` parameter (threaded all the way
//    through from fi_initialize()) is accepted but never read anywhere in
//    Dataset::addVariable()'s body (core/src/dataset.cc) -- confirmed by
//    reading the function, not assumed. "fi_initialize threads nfiles
//    through" below pins that it currently has NO observable effect,
//    rather than testing for an effect that doesn't exist.
//
// The circular dependency this plan's round-3 survey found (file_netcdf.cc
// calling back UP into file.cc's fi_scannable_dims()/fi_n_dims() --
// file_netcdf.cc:200,455,552) is exercised implicitly by every test below
// that calls a netcdf_*() list/lookup function with `file_type` set, and
// explicitly by the two tests under "the circular call-back" further down.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/dataset.h"
#include "ncview/protos.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::SessionFixture;

namespace {

// Writes a (time, lat, lon) file: temp(time,lat,lon) with units/long_name,
// lat/lon coordinate variables with units/long_name, and a "time" record
// (NC_UNLIMITED) dimension with a units attribute and, if non-empty, a
// calendar attribute -- enough surface to exercise every fi_*() forwarder
// at least once. Returns the path; caller must std::remove() it.
std::string make_layer_test_file(const char *var_name, int nt, int nlat, int nlon,
                                  const char *time_units, const char *calendar = "") {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_layer_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_time, dim_lat, dim_lon;
    REQUIRE(nc_def_dim(ncid, "time", NC_UNLIMITED, &dim_time) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lat", nlat, &dim_lat) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lon", nlon, &dim_lon) == NC_NOERR);

    int var_time, var_lat, var_lon, var_data;
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_time, "units", strlen(time_units), time_units) == NC_NOERR);
    if (calendar[0] != '\0')
        REQUIRE(nc_put_att_text(ncid, var_time, "calendar", strlen(calendar), calendar) == NC_NOERR);

    REQUIRE(nc_def_var(ncid, "lat", NC_FLOAT, 1, &dim_lat, &var_lat) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_lat, "units", 13, "degrees_north") == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_lat, "long_name", 8, "Latitude") == NC_NOERR);

    REQUIRE(nc_def_var(ncid, "lon", NC_FLOAT, 1, &dim_lon, &var_lon) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_lon, "units", 12, "degrees_east") == NC_NOERR);

    int dims[3] = {dim_time, dim_lat, dim_lon};
    REQUIRE(nc_def_var(ncid, var_name, NC_FLOAT, 3, dims, &var_data) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_data, "units", 1, "K") == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_data, "long_name", 11, "temperature") == NC_NOERR);

    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    std::vector<double> tvals(nt);
    for (int t = 0; t < nt; t++) tvals[t] = (double)t;
    size_t t_start[1] = {0}, t_count[1] = {(size_t)nt};
    REQUIRE(nc_put_vara_double(ncid, var_time, t_start, t_count, tvals.data()) == NC_NOERR);

    std::vector<float> latvals(nlat), lonvals(nlon);
    for (int i = 0; i < nlat; i++) latvals[i] = (float)i;
    for (int j = 0; j < nlon; j++) lonvals[j] = (float)j;
    REQUIRE(nc_put_var_float(ncid, var_lat, latvals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_lon, lonvals.data()) == NC_NOERR);

    std::vector<float> data((size_t)nt * nlat * nlon);
    for (size_t i = 0; i < data.size(); i++) data[i] = (float)i;
    size_t d_start[3] = {0, 0, 0}, d_count[3] = {(size_t)nt, (size_t)nlat, (size_t)nlon};
    REQUIRE(nc_put_vara_float(ncid, var_data, d_start, d_count, data.data()) == NC_NOERR);

    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

// Runs determine_file_type() on `path` -- the only way, outside file.cc, to
// set the file_type static every fi_*() forwarder dispatches on. Every test
// below needs this before calling anything in file.cc or file_netcdf.cc.
void run_determine_file_type(const std::string &path) {
    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
}

// Opens `path` via netcdf_fi_initialize() directly (NOT fi_initialize(),
// and NOT routed through Dataset) -- for the forwarder-equivalence tests,
// which want a bare fileid to call both the fi_*() and netcdf_*() side of
// each pair on, without a Dataset entry complicating cleanup. Matches
// test_file_netcdf.cc's/test_dataset.cc's own open_sample_file() pattern.
int open_bare(const std::string &path) {
    run_determine_file_type(path);
    return netcdf_fi_initialize(const_cast<char *>(path.c_str()));
}

// A file + bare fileid, auto-closed via the real fi_close() so file_type
// stays exercised on the close path too.
struct BareFile {
    std::string path;
    int fileid;
    static constexpr int nt = 3, nlat = 4, nlon = 5;
    explicit BareFile(const char *var_name, const char *time_units = "days since 2000-01-01",
                       const char *calendar = "")
        : path(make_layer_test_file(var_name, nt, nlat, nlon, time_units, calendar)),
          fileid(open_bare(path)) {}
    ~BareFile() {
        fi_close(fileid);
        std::remove(path.c_str());
    }
};

} // namespace

// ===================== The 13 pure-forwarder fi_*() functions =====================
// Each asserted to return exactly what its netcdf_*() counterpart returns
// for the same fileid/args -- a deliberately mechanical table whose value
// is that it must still pass, unchanged, after Phase 6 deletes file.cc's
// copy of each dispatch.

TEST_CASE("fi_list_vars forwards to netcdf_fi_list_vars") {
    BareFile f("layer_list_vars");
    Stringlist *from_fi = fi_list_vars(f.fileid);
    Stringlist *from_netcdf = netcdf_fi_list_vars(f.fileid);
    REQUIRE(from_fi != nullptr);
    REQUIRE(from_netcdf != nullptr);
    REQUIRE(stringlist_len(from_fi) == stringlist_len(from_netcdf));
    for (size_t i = 0; i < from_fi->size(); i++)
        CHECK((*from_fi)[i].string == (*from_netcdf)[i].string);
}

TEST_CASE("fi_title forwards to netcdf_title") {
    BareFile f("layer_title");
    CHECK(fi_title(f.fileid) == netcdf_title(f.fileid));
}

TEST_CASE("fi_long_var_name forwards to netcdf_long_var_name") {
    BareFile f("layer_long_name");
    CHECK(fi_long_var_name(f.fileid, "layer_long_name") == netcdf_long_var_name(f.fileid, "layer_long_name"));
    CHECK(fi_long_var_name(f.fileid, "layer_long_name") == "temperature");
}

TEST_CASE("fi_var_units forwards to netcdf_var_units") {
    BareFile f("layer_var_units");
    CHECK(fi_var_units(f.fileid, "layer_var_units") == netcdf_var_units(f.fileid, "layer_var_units"));
    CHECK(fi_var_units(f.fileid, "layer_var_units") == "K");
}

TEST_CASE("fi_dim_units forwards to netcdf_dim_units") {
    BareFile f("layer_dim_units");
    CHECK(fi_dim_units(f.fileid, "lat") == netcdf_dim_units(f.fileid, "lat"));
    CHECK(fi_dim_units(f.fileid, "lat") == "degrees_north");
}

TEST_CASE("fi_n_dims forwards to netcdf_fi_n_dims") {
    BareFile f("layer_n_dims");
    CHECK(fi_n_dims(f.fileid, (char *)"layer_n_dims") == netcdf_fi_n_dims(f.fileid, (char *)"layer_n_dims"));
    CHECK(fi_n_dims(f.fileid, (char *)"layer_n_dims") == 3);
}

TEST_CASE("fi_scannable_dims forwards to netcdf_scannable_dims") {
    BareFile f("layer_scannable");
    Stringlist *from_fi = fi_scannable_dims(f.fileid, (char *)"layer_scannable");
    Stringlist *from_netcdf = netcdf_scannable_dims(f.fileid, (char *)"layer_scannable");
    REQUIRE(from_fi != nullptr);
    REQUIRE(from_netcdf != nullptr);
    REQUIRE(stringlist_len(from_fi) == stringlist_len(from_netcdf));
    for (size_t i = 0; i < from_fi->size(); i++)
        CHECK((*from_fi)[i].string == (*from_netcdf)[i].string);
}

TEST_CASE("fi_var_size forwards to netcdf_fi_var_size") {
    BareFile f("layer_var_size");
    size_t *from_fi = fi_var_size(f.fileid, (char *)"layer_var_size");
    size_t *from_netcdf = netcdf_fi_var_size(f.fileid, (char *)"layer_var_size");
    REQUIRE(from_fi != nullptr);
    REQUIRE(from_netcdf != nullptr);
    for (int i = 0; i < 3; i++)
        CHECK(from_fi[i] == from_netcdf[i]);
    CHECK(from_fi[0] == BareFile::nt);
    CHECK(from_fi[1] == BareFile::nlat);
    CHECK(from_fi[2] == BareFile::nlon);
}

TEST_CASE("fi_dim_id_to_name forwards to netcdf_dim_id_to_name") {
    BareFile f("layer_dim_id_to_name");
    CHECK(fi_dim_id_to_name(f.fileid, "layer_dim_id_to_name", 1) ==
          netcdf_dim_id_to_name(f.fileid, "layer_dim_id_to_name", 1));
    CHECK(fi_dim_id_to_name(f.fileid, "layer_dim_id_to_name", 1) == "lat");
}

TEST_CASE("fi_dim_name_to_id forwards to netcdf_dim_name_to_id") {
    BareFile f("layer_dim_name_to_id");
    CHECK(fi_dim_name_to_id(f.fileid, (char *)"layer_dim_name_to_id", (char *)"lat") ==
          netcdf_dim_name_to_id(f.fileid, (char *)"layer_dim_name_to_id", (char *)"lat"));
    CHECK(fi_dim_name_to_id(f.fileid, (char *)"layer_dim_name_to_id", (char *)"lat") == 1);
    // A dim name that doesn't exist on the variable: both sides must agree
    // on the -1 miss too, not just the hit.
    CHECK(fi_dim_name_to_id(f.fileid, (char *)"layer_dim_name_to_id", (char *)"nope") == -1);
}

TEST_CASE("fi_dim_longname forwards to netcdf_dim_longname") {
    BareFile f("layer_dim_longname");
    CHECK(fi_dim_longname(f.fileid, "lat") == netcdf_dim_longname(f.fileid, "lat"));
    CHECK(fi_dim_longname(f.fileid, "lat") == "Latitude");
}

TEST_CASE("fi_recdim_id forwards to netcdf_fi_recdim_id -- and has no file_type guard at all") {
    // Unlike every other fi_*() forwarder, fi_recdim_id() (file.cc) has no
    // `if (file_type != FILE_TYPE_NETCDF)` check at all -- it unconditionally
    // calls netcdf_fi_recdim_id(). That makes this equivalence trivially
    // true by construction rather than by dispatch, which is itself the
    // behavior being pinned: Phase 6 must not "fix" this by adding a guard
    // as an incidental side effect of the collapse, since that would be a
    // behavior change riding along on a refactor.
    BareFile f("layer_recdim_id");
    CHECK(fi_recdim_id(f.fileid) == netcdf_fi_recdim_id(f.fileid));
    CHECK(fi_recdim_id(f.fileid) >= 0); // "time" is the unlimited dim
}

TEST_CASE("fi_fill_aux_data forwards to netcdf_fill_aux_data") {
    BareFile f("layer_fill_aux");
    // Neither function reads fdb->file (only ->filename/->recdim_units/
    // ->aux_data/->ut_unit_ptr are touched), so two bare FDBlists with no
    // NetCDFFile attached are enough to compare -- avoids constructing a
    // second NetCDFFile over the same already-tracked fileid, which would
    // double-close it. aux_data DOES have to be pre-allocated, though:
    // netcdf_fill_aux_data() unconditionally dereferences fdb->aux_data.get()
    // once the target variable has any attributes at all (it does here --
    // "units"/"long_name") with no null check, exactly as new_fdblist()
    // (dataset.cc), the only production caller, always pre-allocates it
    // before calling. Leaving it default-constructed (nullptr) here
    // reproduces a real SIGSEGV, confirmed while writing this test -- see
    // this file's PORTING.md writeup.
    FDBlist via_fi;
    via_fi.filename = f.path;
    via_fi.aux_data = std::make_unique<NetCDFOptions>();
    fi_fill_aux_data(f.fileid, (char *)"layer_fill_aux", &via_fi);

    FDBlist via_netcdf;
    via_netcdf.filename = f.path;
    via_netcdf.aux_data = std::make_unique<NetCDFOptions>();
    netcdf_fill_aux_data(f.fileid, (char *)"layer_fill_aux", &via_netcdf);

    CHECK(via_fi.recdim_units == via_netcdf.recdim_units);
    CHECK(via_fi.recdim_units == "days since 2000-01-01");
    CHECK((via_fi.aux_data != nullptr) == (via_netcdf.aux_data != nullptr));
}

// ===================== fi_dim_calendar's override branch =====================

TEST_CASE("fi_dim_calendar: with no command-line override, forwards to netcdf_dim_calendar") {
    SessionFixture fx; // options.calendar defaults to empty on a fresh session
    BareFile f("layer_calendar_forward", "days since 2000-01-01", "noleap");
    CHECK(options.calendar.empty());
    CHECK(fi_dim_calendar(f.fileid, "time") == netcdf_dim_calendar(f.fileid, "time"));
    CHECK(fi_dim_calendar(f.fileid, "time") == "noleap");
}

TEST_CASE("fi_dim_calendar: options.calendar, when set, overrides the file's own calendar attribute") {
    SessionFixture fx;
    BareFile f("layer_calendar_override", "days since 2000-01-01", "noleap");
    REQUIRE(fi_dim_calendar(f.fileid, "time") == "noleap"); // file's own attribute, unset override

    options.calendar = "360_day";
    // file.cc:151-155 -- the command line wins outright; it doesn't even
    // consult the file for this dim once options.calendar is non-empty.
    CHECK(fi_dim_calendar(f.fileid, "time") == "360_day");
}

// ===================== determine_file_type =====================

TEST_CASE("determine_file_type: accepts a real netCDF file and sets file_type for later fi_*() calls") {
    // The rejection path (a non-netCDF or nonexistent file) is NOT tested
    // here: determine_file_type() exit(-1)s outright on failure (file.cc),
    // which would terminate the test binary rather than fail one assertion.
    // This is a known, deliberate coverage gap -- see this file's header
    // comment.
    BareFile f("layer_determine_type"); // BareFile's ctor already ran determine_file_type()
    // If file_type hadn't been set to FILE_TYPE_NETCDF, this and every
    // other fi_*() call in this file would have exit(-1)'d already.
    CHECK(fi_n_dims(f.fileid, (char *)"layer_determine_type") == 3);
}

// ===================== fi_initialize: open + fi_list_vars + Dataset::addVariables =====================

TEST_CASE("fi_initialize: opens the file and adds its variable(s) to the Dataset") {
    SessionFixture fx;
    ensure_ncview_misc_initialized();
    std::string path = make_layer_test_file("layer_fi_init", 3, 4, 5, "days since 2000-01-01");
    run_determine_file_type(path);

    int fileid = fi_initialize(const_cast<char *>(path.c_str()), /*nfiles=*/1);

    NCVar *var = g_dataset.findVariable("layer_fi_init");
    REQUIRE(var != nullptr);
    CHECK(var->n_dims == 3);
    REQUIRE(var->files.size() == 1);
    CHECK(var->files[0]->id() == fileid);
    CHECK(var->is_virtual == false);

    std::remove(path.c_str());
}

TEST_CASE("fi_initialize: the nfiles argument is threaded through but has no currently-observable effect") {
    // Confirmed by reading Dataset::addVariable() (dataset.cc): its `nfiles`
    // parameter is accepted and passed down from fi_initialize() but never
    // read anywhere in the function body. This test pins that fact --
    // calling fi_initialize() with two different nfiles values on
    // otherwise-identical single-file opens produces an identical resulting
    // NCVar, not a difference the plan's original wording ("assert the
    // nfiles argument's effect") assumed existed. If a future change makes
    // nfiles actually matter, this test will need updating, which is the
    // point of pinning it now rather than leaving the assumption untested.
    SessionFixture fx1;
    ensure_ncview_misc_initialized();
    std::string path1 = make_layer_test_file("layer_nfiles_a", 3, 4, 5, "days since 2000-01-01");
    run_determine_file_type(path1);
    int fileid1 = fi_initialize(const_cast<char *>(path1.c_str()), /*nfiles=*/1);
    NCVar *var1 = g_dataset.findVariable("layer_nfiles_a");
    REQUIRE(var1 != nullptr);
    size_t n_dims1 = var1->n_dims;
    size_t size0_1 = var1->size[0];
    bool virtual1 = var1->is_virtual;
    std::remove(path1.c_str());

    SessionFixture fx2;
    ensure_ncview_misc_initialized();
    std::string path2 = make_layer_test_file("layer_nfiles_a", 3, 4, 5, "days since 2000-01-01");
    run_determine_file_type(path2);
    int fileid2 = fi_initialize(const_cast<char *>(path2.c_str()), /*nfiles=*/17);
    NCVar *var2 = g_dataset.findVariable("layer_nfiles_a");
    REQUIRE(var2 != nullptr);

    CHECK(var2->n_dims == n_dims1);
    CHECK(var2->size[0] == size0_1);
    CHECK(var2->is_virtual == virtual1);
    (void)fileid1;
    (void)fileid2;
    std::remove(path2.c_str());
}

// ===================== The circular call-back =====================
// file_netcdf.cc calls back UP into file.cc (fi_scannable_dims() at
// file_netcdf.cc:200, fi_n_dims() at :455 and :552) -- confirmed by the
// round-3 survey and, independently, by test_file_netcdf.cc's own header
// comment. Both tests below exist specifically as a safety net for Phase 6,
// which has to break this cycle deliberately rather than by accident.

TEST_CASE("the circular call-back: netcdf_fi_list_vars calls back into fi_scannable_dims") {
    // netcdf_fi_list_vars()'s inner loop (file_netcdf.cc:191-216) calls
    // fi_scannable_dims() -- not netcdf_scannable_dims() directly -- to
    // decide whether a variable belongs on the displayable list. With
    // file_type properly set, this round-trip must succeed and the data
    // variable (which has 2 scannable dims of size >1: lat, lon) must show
    // up on the list.
    BareFile f("layer_circular_scannable");
    Stringlist *vars = netcdf_fi_list_vars(f.fileid);
    REQUIRE(vars != nullptr);
    bool found = false;
    for (const auto &e : *vars) if (e.string == "layer_circular_scannable") found = true;
    CHECK(found);
}

TEST_CASE("the circular call-back: netcdf_dim_name_to_id/netcdf_dim_id_to_name call back into fi_n_dims") {
    // netcdf_dim_name_to_id() and netcdf_dim_id_to_name() (file_netcdf.cc:
    // 455, 552) both call fi_n_dims() -- not netcdf_fi_n_dims() -- to learn
    // how many dims the variable has before resolving the requested one.
    // With file_type set this must resolve correctly in both directions.
    BareFile f("layer_circular_ndims");
    int lat_id = netcdf_dim_name_to_id(f.fileid, (char *)"layer_circular_ndims", (char *)"lon");
    CHECK(lat_id == 2); // third dim of (time,lat,lon)
    std::string name = netcdf_dim_id_to_name(f.fileid, (char *)"layer_circular_ndims", lat_id);
    CHECK(name == "lon");
}

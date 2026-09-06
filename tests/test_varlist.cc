// Characterization tests for the variable-list machinery in
// core/src/util.cc: add_var_to_list() (and the add_to_varlist()/
// new_variable()/new_fdblist() helpers it calls), get_var(), is_scannable(),
// and n_vars_in_list(). This is the code Phase 5 of modernization.md
// replaces wholesale (NCVar/FDBlist's void*/AnyPtr-based intrusive linked
// lists become std::vector<std::unique_ptr<...>>), and it had no test
// before this file -- test_file_netcdf.cc only exercises the netcdf_*()
// dispatch layer below it, never the NCVar-building logic on top.
//
// The one behavior this test exists specifically to pin down: when the
// same variable spans more than one input file (a "virtual" variable, in
// upstream's own terminology -- e.g. one year of monthly data per file),
// var->size (the NCVar-level accumulated size) grows as each file is
// added, but each FDBlist's own var_size stays that file's real size, and
// (more subtly) the NCDim objects in var->dim[] are only ever populated
// from the FIRST file's fill_dim_structs() call -- dim->size does NOT
// track the accumulated total the way var->size does. Getting this
// relationship wrong in Phase 5's rewrite would be exactly the kind of
// silent, hard-to-notice regression a characterization test is for.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "test_udunits_helper.h"

namespace {

// Writes a (time, lat, lon) file: a "temp" data variable plus time/lat/lon
// coordinate variables, with the time axis starting at time_offset_days and
// spaced 1 day apart -- enough for handle_time_dim() to recognize it as a
// real UDUNITS time axis (see core/src/util.cc:handle_time_dim()).
std::string make_virtual_piece(const char *var_name, int nt, int nlat, int nlon,
                                double time_offset_days) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_varlist_XXXXXX").string();
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
    for (int i = 0; i < nt; i++) tvals[i] = time_offset_days + i;
    REQUIRE(nc_put_var_double(ncid, var_time, tvals.data()) == NC_NOERR);
    std::vector<float> latvals(nlat, 0.0f), lonvals(nlon, 0.0f);
    for (int i = 0; i < nlat; i++) latvals[i] = (float)i;
    for (int i = 0; i < nlon; i++) lonvals[i] = (float)i;
    REQUIRE(nc_put_var_float(ncid, var_lat, latvals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_lon, lonvals.data()) == NC_NOERR);
    std::vector<float> data(nt * nlat * nlon, 1.0f);
    REQUIRE(nc_put_var_float(ncid, var_data, data.data()) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

int open_for_core(const std::string &path) {
    Stringlist *files = nullptr;
    stringlist_add_string(&files, const_cast<char *>(path.c_str()), nullptr, SLTYPE_NULL);
    determine_file_type(files);
    stringlist_delete_entire_list(files);
    return netcdf_fi_initialize(const_cast<char *>(path.c_str()));
}

} // namespace

TEST_CASE("is_scannable: dim 0 (the record dim) is always scannable, others need size>1") {
    size_t size[3] = {1, 1, 4};
    NCVar var{};
    var.size = size;

    CHECK(is_scannable(&var, 0) != 0);  // dim 0 is special-cased true regardless of size
    CHECK(is_scannable(&var, 1) == 0);  // size 1, not dim 0 -> not scannable
    CHECK(is_scannable(&var, 2) != 0);  // size 4 -> scannable
}

TEST_CASE("n_vars_in_list: counts a NULL list as zero and walks ->next") {
    CHECK(n_vars_in_list(nullptr) == 0);

    NCVar a{}, b{}, c{};
    a.next = &b;
    b.next = &c;
    c.next = nullptr;
    CHECK(n_vars_in_list(&a) == 3);
}

TEST_CASE("add_var_to_list: a variable spanning two files becomes virtual, "
          "accumulates var->size, but each file's own FDBlist keeps its own size") {
    ensure_ncview_misc_initialized();

    // Use a variable name unique to this test case (the global `variables`
    // list persists for the whole test binary's lifetime -- see util.cc's
    // get_var(), a plain linear scan with no removal API), so this can't
    // collide with any other TEST_CASE's variable.
    const char *var_name = "temp_virtual_test";
    std::string path1 = make_virtual_piece(var_name, 3, 2, 2, 0.0);   // 3 timesteps
    std::string path2 = make_virtual_piece(var_name, 2, 2, 2, 3.0);  // 2 more, contiguous

    int nvars_before = n_vars_in_list(variables);

    int fid1 = open_for_core(path1);
    add_var_to_list(const_cast<char *>(var_name), fid1, const_cast<char *>(path1.c_str()), 2);

    NCVar *var = get_var(const_cast<char *>(var_name));
    REQUIRE(var != nullptr);
    CHECK(var->is_virtual == false); // only one file so far
    CHECK(var->size[0] == 3);        // time
    CHECK(var->n_dims == 3);
    CHECK(n_vars_in_list(variables) == nvars_before + 1); // exactly one new NCVar

    int fid2 = open_for_core(path2);
    add_var_to_list(const_cast<char *>(var_name), fid2, const_cast<char *>(path2.c_str()), 2);

    // Re-fetch: add_var_to_list() mutates the existing NCVar in place for a
    // variable it already knows about, so `var` is still valid, but re-
    // fetching documents that get_var() finds the same, not a new, node.
    NCVar *var2 = get_var(const_cast<char *>(var_name));
    CHECK(var2 == var);
    CHECK(var->is_virtual == true);
    CHECK(var->size[0] == 5); // 3 + 2, accumulated across both files
    CHECK(n_vars_in_list(variables) == nvars_before + 1); // still exactly one NCVar

    // The two FDBlist entries are a real doubly-linked (AnyPtr-based) list,
    // each keeping its OWN file's size, not the accumulated total.
    FDBlist *f0 = var->first_file;
    REQUIRE(f0 != nullptr);
    CHECK(f0->index == 0);
    CHECK(f0->var_size[0] == 3);
    CHECK(f0->prev == nullptr);

    FDBlist *f1 = f0->next; // AnyPtr -- no chained ->, see anyptr.h
    REQUIRE(f1 != nullptr);
    CHECK(f1->index == 1);
    CHECK(f1->var_size[0] == 2);
    CHECK(f1 == var->last_file);
    FDBlist *f1_prev = f1->prev;
    CHECK(f1_prev == f0);
    CHECK(static_cast<FDBlist *>(f1->next) == nullptr);

    // var->dim[] is only ever populated from the FIRST file's
    // fill_dim_structs() call (see util.cc:add_var_to_list()'s "already
    // exists" branch -- it never re-derives dim structs for later files),
    // so dim->size reflects just that first file's 3 timesteps, NOT the
    // accumulated 5 that var->size[0] now holds. This asymmetry is exactly
    // the kind of detail Phase 5's NCVar/FDBlist rewrite must preserve.
    REQUIRE(var->dim[0] != nullptr);
    CHECK(std::strcmp(var->dim[0]->name, "time") == 0);
    CHECK(var->dim[0]->size == 3);
    CHECK(var->dim[0]->timelike == 1); // handle_time_dim() recognized the udunits time axis

    netcdf_fi_close(fid1);
    netcdf_fi_close(fid2);
    std::remove(path1.c_str());
    std::remove(path2.c_str());
}

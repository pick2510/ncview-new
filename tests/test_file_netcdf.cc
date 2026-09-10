// Copyright (C) 2026 Dominik Strebel
//
// Integration tests for core/src/file_netcdf.cc against a real, synthetic
// netCDF file (written and read back via the plain netCDF C API) -- this is
// the file-I/O boundary the rest of core's pure-logic tests deliberately
// don't exercise.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

namespace {

// HDF5 (netCDF-4's storage backend) has taken out an advisory file lock on
// every open file by default since 1.10.0. That's actively hostile to a
// test that creates, closes, and immediately reopens the same short-lived
// file: confirmed on CI as an outright hang on Windows (the process never
// returned; no crash, no timeout from netCDF itself, just stuck) the first
// time this test actually got far enough to hit real file I/O rather than
// failing fast on an earlier bug. This is a well-known HDF5 gotcha on
// Windows, network filesystems, and various CI sandboxes -- HDF5_USE_FILE_
// LOCKING=FALSE is the documented escape hatch. Must be set before the
// first netCDF/HDF5 call in the process.
struct DisableHdf5FileLocking {
    DisableHdf5FileLocking() {
#ifdef _WIN32
        _putenv_s( "HDF5_USE_FILE_LOCKING", "FALSE" );
#else
        setenv( "HDF5_USE_FILE_LOCKING", "FALSE", 0 );
#endif
    }
} g_disable_hdf5_file_locking;

// Creates a small netCDF file with dims time(3), lat(4), lon(5) and
// variables lat(lat), lon(lon), time(time), temp(time,lat,lon); returns its
// path. Caller must std::remove() it.
std::string make_sample_file() {
    // A hardcoded "/tmp/..." template isn't valid on Windows -- mkstemp()
    // itself is portable (mingw-w64 provides it), but the path needs to
    // come from the platform's actual temp directory.
    std::string path_template =
        (std::filesystem::temp_directory_path() / "ncview_test_XXXXXX").string();
    int fd = mkstemp(&path_template[0]);
    REQUIRE(fd >= 0);
    close(fd); // nc_create() below re-creates it; mkstemp() just reserves a unique name.
    std::string path = path_template;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);

    int dim_time, dim_lat, dim_lon;
    REQUIRE(nc_def_dim(ncid, "time", 3, &dim_time) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lat", 4, &dim_lat) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lon", 5, &dim_lon) == NC_NOERR);

    int var_time, var_lat, var_lon, var_temp;
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_time, "units", 21, "days since 2000-01-01") == NC_NOERR);

    REQUIRE(nc_def_var(ncid, "lat", NC_FLOAT, 1, &dim_lat, &var_lat) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_lat, "units", 13, "degrees_north") == NC_NOERR);

    REQUIRE(nc_def_var(ncid, "lon", NC_FLOAT, 1, &dim_lon, &var_lon) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_lon, "units", 12, "degrees_east") == NC_NOERR);

    int temp_dims[3] = {dim_time, dim_lat, dim_lon};
    REQUIRE(nc_def_var(ncid, "temp", NC_FLOAT, 3, temp_dims, &var_temp) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_temp, "units", 1, "K") == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_temp, "long_name", 11, "temperature") == NC_NOERR);
    float fill = -999.0f;
    REQUIRE(nc_put_att_float(ncid, var_temp, "_FillValue", NC_FLOAT, 1, &fill) == NC_NOERR);

    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    double time_vals[3] = {0.0, 1.0, 2.0};
    REQUIRE(nc_put_var_double(ncid, var_time, time_vals) == NC_NOERR);
    float lat_vals[4] = {10.0f, 20.0f, 30.0f, 40.0f};
    REQUIRE(nc_put_var_float(ncid, var_lat, lat_vals) == NC_NOERR);
    float lon_vals[5] = {-100.0f, -90.0f, -80.0f, -70.0f, -60.0f};
    REQUIRE(nc_put_var_float(ncid, var_lon, lon_vals) == NC_NOERR);

    float temp_vals[3 * 4 * 5];
    for (int i = 0; i < 3 * 4 * 5; i++) temp_vals[i] = (float)i;
    REQUIRE(nc_put_var_float(ncid, var_temp, temp_vals) == NC_NOERR);

    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

// UPDATE (Phase 6): this comment used to say netcdf_dim_name_to_id()/
// netcdf_dim_id_to_name() internally called the dispatching fi_n_dims() --
// that's no longer true (Phase 6 broke that circular dependency; they call
// netcdf_fi_n_dims() directly now, confirmed by reading file_netcdf.cc, not
// assumed) and no function this file exercises still checks file_type via
// a fi_*() dispatcher. determine_file_type() is called below regardless,
// since it's the only way core's file_type module-static gets set at all
// and other tests/production code depend on it having run by this point in
// the process; it needs a real file to probe, so this runs after the
// sample file exists but before opening it for real.
int open_sample_file(const std::string &path) {
    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
    return netcdf_fi_initialize(const_cast<char *>(path.c_str()));
}

// RAII wrapper: opens the sample file via the same entry points core
// itself uses, and always cleans up the fileid + tmp file.
struct SampleFile {
    std::string path;
    int fileid;
    SampleFile() : path(make_sample_file()), fileid(open_sample_file(path)) {}
    ~SampleFile() {
        netcdf_fi_close(fileid);
        std::remove(path.c_str());
    }
};

// A one-variable file for netcdf_fill_value()/netcdf_fill_aux_data() tests,
// with a caller-supplied set of float attributes on the data variable (any
// of missing_value/_FillValue/scale_factor/add_offset, or a global
// missing_value) -- covers the attribute-precedence and unpacking logic
// that, before this, had zero test coverage anywhere (confirmed by grep;
// "refine the architecture" plan, Phase 6).
struct FillValueFile {
    std::string path;
    int fileid;

    FillValueFile( std::optional<float> var_missing_value,
                   std::optional<float> fill_value_attr,
                   std::optional<float> global_missing_value,
                   std::optional<float> scale_factor,
                   std::optional<float> add_offset,
                   std::optional<float> valid_min = std::nullopt,
                   std::optional<float> valid_max = std::nullopt,
                   bool add_unrelated_units_attr = false ) {
        auto tmpl = (std::filesystem::temp_directory_path() / "ncview_fillval_XXXXXX").string();
        int fd = mkstemp( &tmpl[0] );
        REQUIRE( fd >= 0 );
        close( fd );
        path = tmpl;

        int ncid;
        REQUIRE( nc_create( path.c_str(), NC_CLOBBER, &ncid ) == NC_NOERR );
        int dim_x;
        REQUIRE( nc_def_dim( ncid, "x", 3, &dim_x ) == NC_NOERR );
        int varid;
        REQUIRE( nc_def_var( ncid, "data", NC_FLOAT, 1, &dim_x, &varid ) == NC_NOERR );

        if( var_missing_value )
            REQUIRE( nc_put_att_float( ncid, varid, "missing_value", NC_FLOAT, 1, &*var_missing_value ) == NC_NOERR );
        if( fill_value_attr )
            REQUIRE( nc_put_att_float( ncid, varid, "_FillValue", NC_FLOAT, 1, &*fill_value_attr ) == NC_NOERR );
        if( global_missing_value )
            REQUIRE( nc_put_att_float( ncid, NC_GLOBAL, "missing_value", NC_FLOAT, 1, &*global_missing_value ) == NC_NOERR );
        if( scale_factor )
            REQUIRE( nc_put_att_float( ncid, varid, "scale_factor", NC_FLOAT, 1, &*scale_factor ) == NC_NOERR );
        if( add_offset )
            REQUIRE( nc_put_att_float( ncid, varid, "add_offset", NC_FLOAT, 1, &*add_offset ) == NC_NOERR );
        if( valid_min )
            REQUIRE( nc_put_att_float( ncid, varid, "valid_min", NC_FLOAT, 1, &*valid_min ) == NC_NOERR );
        if( valid_max )
            REQUIRE( nc_put_att_float( ncid, varid, "valid_max", NC_FLOAT, 1, &*valid_max ) == NC_NOERR );
        if( add_unrelated_units_attr )
            REQUIRE( nc_put_att_text( ncid, varid, "units", 1, "K" ) == NC_NOERR );

        REQUIRE( nc_enddef( ncid ) == NC_NOERR );
        float vals[3] = { 1.0f, 2.0f, 3.0f };
        REQUIRE( nc_put_var_float( ncid, varid, vals ) == NC_NOERR );
        REQUIRE( nc_close( ncid ) == NC_NOERR );

        fileid = open_sample_file( path );
    }
    ~FillValueFile() {
        netcdf_fi_close( fileid );
        std::remove( path.c_str() );
    }
};

} // namespace

TEST_CASE("file_netcdf: n_dims and var_size match the variable's real shape") {
    SampleFile f;
    CHECK(netcdf_fi_n_dims(f.fileid, (char *)"temp") == 3);
    CHECK(netcdf_fi_n_dims(f.fileid, (char *)"lat") == 1);

    size_t *size = netcdf_fi_var_size(f.fileid, (char *)"temp");
    REQUIRE(size != nullptr);
    CHECK(size[0] == 3); // time
    CHECK(size[1] == 4); // lat
    CHECK(size[2] == 5); // lon
}

TEST_CASE("file_netcdf: scannable_dims lists every dim of a 3-D variable") {
    SampleFile f;
    Stringlist *dims = netcdf_scannable_dims(f.fileid, (char *)"temp");
    REQUIRE(dims != nullptr);
    CHECK(stringlist_len(dims) == 3);
    REQUIRE(dims->size() == 3);
    CHECK((*dims)[0].string == "time");
    CHECK((*dims)[1].string == "lat");
    CHECK((*dims)[2].string == "lon");
}

TEST_CASE("file_netcdf: dim name/id lookups round-trip") {
    SampleFile f;
    int lat_id = netcdf_dim_name_to_id(f.fileid, (char *)"temp", (char *)"lat");
    CHECK(lat_id == 1); // second dim of temp(time,lat,lon)

    std::string name = netcdf_dim_id_to_name(f.fileid, (char *)"temp", lat_id);
    CHECK(name == "lat");

    CHECK(netcdf_dim_name_to_id(f.fileid, (char *)"temp", (char *)"not_a_dim") == -1);
}

TEST_CASE("file_netcdf: var and dim units come back as written") {
    SampleFile f;
    CHECK(netcdf_var_units(f.fileid, (char *)"temp") == "K");
    CHECK(netcdf_dim_units(f.fileid, (char *)"lat") == "degrees_north");
    CHECK(netcdf_long_var_name(f.fileid, (char *)"temp") == "temperature");
}

TEST_CASE("file_netcdf: dim value reads back real coordinate data") {
    SampleFile f;
    CHECK(netcdf_has_dim_values(f.fileid, (char *)"lat") != 0);

    double val;
    char cval[256];
    int has_bounds;
    double bmin, bmax;
    nc_type type = netcdf_dim_value(f.fileid, (char *)"lat", 2, &val, cval, 0,
                                     &has_bounds, &bmin, &bmax);
    // Numeric dimvars are always normalized to NC_DOUBLE on the way out
    // (only NC_CHAR dimvars keep their own type) -- see the case block in
    // netcdf_dim_value().
    CHECK(type == NC_DOUBLE);
    CHECK(val == doctest::Approx(30.0));
    CHECK(has_bounds == 0);
}

TEST_CASE("netcdf_dim_value: an unsigned-typed (netCDF-4) coordinate variable reports no bounds, not garbage (Phase 12f)") {
    // Every type netcdf_dim_value() actually handles by name (NC_BYTE/
    // SHORT/LONG/FLOAT/DOUBLE/INT64/CHAR) was already covered by the test
    // above. Any netCDF-4 numeric type added since 1993 -- NC_UINT here --
    // falls through to the function's `default:` case, which before Phase
    // 12f never wrote *return_has_bounds at all: a caller reading it was
    // reading whatever garbage happened to be on its own stack. This test
    // can only prove the value is now deterministically 0, not that it was
    // previously garbage on this exact run -- ASan/UBSan don't catch reads
    // of uninitialized *stack* memory (that needs MSan or Valgrind, neither
    // configured in this project).
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_uint_dim_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_NETCDF4 | NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_lev;
    REQUIRE(nc_def_dim(ncid, "lev", 3, &dim_lev) == NC_NOERR);
    int var_lev;
    REQUIRE(nc_def_var(ncid, "lev", NC_UINT, 1, &dim_lev, &var_lev) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);
    unsigned int lev_vals[3] = {100u, 200u, 300u};
    REQUIRE(nc_put_var_uint(ncid, var_lev, lev_vals) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);

    int fileid = open_sample_file(path);

    double val;
    char cval[256];
    int has_bounds;
    double bmin, bmax;
    nc_type type = netcdf_dim_value(fileid, (char *)"lev", 1, &val, cval, 0,
                                     &has_bounds, &bmin, &bmax);
    // The default branch falls back to the virtual place, not the real
    // (unreadable-as-double-by-name) value.
    CHECK(type == NC_DOUBLE);
    CHECK(has_bounds == 0);
    CHECK(bmin == 0.0);
    CHECK(bmax == 0.0);

    netcdf_fi_close(fileid);
    std::remove(path.c_str());
}

TEST_CASE("file_netcdf: a name with no matching dimvar reports no values") {
    SampleFile f;
    // netcdf_has_dim_values()'s notion of "dimvar" is purely name-based (a
    // variable named identically to the dim, per netCDF's own coordinate-
    // variable convention -- see netcdf_dimvar_id()), with no cross-check
    // that a same-named dimension actually exists. A name matching nothing
    // in the file at all must not crash, just report no dim values.
    CHECK(netcdf_has_dim_values(f.fileid, (char *)"nonexistent") == 0);
}

// ===================== netcdf_fill_value(): attribute precedence and unpacking =====================
//
// Phase 6 of the "refine the architecture" plan: netcdf_fill_value()'s
// _FillValue/missing_value/valid_range precedence and its scale_factor/
// add_offset unpacking had zero test coverage anywhere (confirmed by grep
// before writing these). netcdf_fill_value() itself doesn't read valid_range
// at all -- that's netcdf_min_max_option_set()/netcdf_get_att_util(),
// exercised separately via Dataset::checkRanges() -- so "valid_range
// precedence" here means what the function's own attribute-checking order
// actually does: three float attributes checked in sequence, each one that
// exists overwriting *v, so the LAST one found wins.

TEST_CASE("netcdf_fill_value: with only _FillValue set, that value is used") {
    FillValueFile f( std::nullopt, 42.0f, std::nullopt, std::nullopt, std::nullopt );
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, nullptr );
    CHECK( v == doctest::Approx(42.0f) );
}

TEST_CASE("netcdf_fill_value: with only missing_value set, that value is used") {
    FillValueFile f( -999.0f, std::nullopt, std::nullopt, std::nullopt, std::nullopt );
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, nullptr );
    CHECK( v == doctest::Approx(-999.0f) );
}

TEST_CASE("netcdf_fill_value: _FillValue overrides a var-level missing_value") {
    // netcdf_fill_value() checks missing_value first, then _FillValue,
    // overwriting *v each time something is found -- so of these two,
    // whichever is checked LAST wins, which is _FillValue.
    FillValueFile f( -999.0f, 42.0f, std::nullopt, std::nullopt, std::nullopt );
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, nullptr );
    CHECK( v == doctest::Approx(42.0f) );
}

TEST_CASE("netcdf_fill_value: a global missing_value overrides both var-level attributes") {
    // The global missing_value check runs last of the three, so it wins
    // over both a var-level missing_value AND a var-level _FillValue --
    // a real, surprising-until-you-read-the-code precedence order.
    FillValueFile f( -999.0f, 42.0f, /*global_missing_value=*/-1.0f, std::nullopt, std::nullopt );
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, nullptr );
    CHECK( v == doctest::Approx(-1.0f) );
}

TEST_CASE("netcdf_fill_value: with no fill-related attribute at all, uses the type's netCDF default") {
    FillValueFile f( std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt );
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, nullptr );
    // "data" is NC_FLOAT.
    CHECK( v == doctest::Approx(NC_FILL_FLOAT) );
}

TEST_CASE("netcdf_fill_value: scale_factor and add_offset both apply to the found fill value") {
    FillValueFile f( std::nullopt, /*fill_value_attr=*/10.0f, std::nullopt,
                      /*scale_factor=*/2.0f, /*add_offset=*/1.0f );
    NetCDFOptions aux{};
    aux.scale_factor_set = true;
    aux.scale_factor = 2.0f;
    aux.add_offset_set = true;
    aux.add_offset = 1.0f;
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, &aux );
    CHECK( v == doctest::Approx(10.0f * 2.0f + 1.0f) );
}

TEST_CASE("netcdf_fill_value: scale_factor alone applies without an offset") {
    FillValueFile f( std::nullopt, /*fill_value_attr=*/10.0f, std::nullopt,
                      /*scale_factor=*/2.0f, std::nullopt );
    NetCDFOptions aux{};
    aux.scale_factor_set = true;
    aux.scale_factor = 2.0f;
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, &aux );
    CHECK( v == doctest::Approx(20.0f) );
}

TEST_CASE("netcdf_fill_value: add_offset alone applies without a scale") {
    FillValueFile f( std::nullopt, /*fill_value_attr=*/10.0f, std::nullopt,
                      std::nullopt, /*add_offset=*/5.0f );
    NetCDFOptions aux{};
    aux.add_offset_set = true;
    aux.add_offset = 5.0f;
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, &aux );
    CHECK( v == doctest::Approx(15.0f) );
}

TEST_CASE("netcdf_fill_value: a null aux_data skips scale/offset unpacking entirely") {
    // Documented at the call site (netcdf_fill_value()'s own comment):
    // aux_data is NULL for coordinate-variable reads, which have no
    // scale/offset attributes to apply. Confirms the found value passes
    // through unscaled rather than crashing on a null aux_data.
    FillValueFile f( std::nullopt, /*fill_value_attr=*/10.0f, std::nullopt, std::nullopt, std::nullopt );
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, nullptr );
    CHECK( v == doctest::Approx(10.0f) );
}

// ===================== netcdf_fill_aux_data(): populating NetCDFOptions from attributes =====================

TEST_CASE("netcdf_fill_aux_data: reads scale_factor/add_offset/valid_min/valid_max off the variable") {
    FillValueFile f( std::nullopt, std::nullopt, std::nullopt, /*scale_factor=*/3.0f, /*add_offset=*/7.0f,
                      /*valid_min=*/-5.0f, /*valid_max=*/5.0f );

    FDBlist fdb;
    fdb.filename = f.path;
    fdb.aux_data = std::make_unique<NetCDFOptions>();
    netcdf_fill_aux_data( f.fileid, (char *)"data", &fdb );

    CHECK( fdb.aux_data->scale_factor_set );
    CHECK( fdb.aux_data->scale_factor == doctest::Approx(3.0f) );
    CHECK( fdb.aux_data->add_offset_set );
    CHECK( fdb.aux_data->add_offset == doctest::Approx(7.0f) );
    CHECK( fdb.aux_data->valid_min_set );
    CHECK( fdb.aux_data->valid_max_set );
    // Confirmed by reading the code, not assumed (this plan's standing
    // rule): with add_offset AND scale_factor both set but no valid_range
    // attribute, netcdf_fill_aux_data()'s "assume they apply to the valid
    // range too" special case (file_netcdf.cc, right after the four
    // netcdf_get_att_util() calls) transforms valid_min/valid_max in
    // place -- so the values read back are NOT the raw -5/5 written above,
    // they're valid_min*scale_factor+add_offset and
    // valid_max*scale_factor+add_offset. First draft of this test
    // expected the raw values and failed; this is real, pre-existing
    // behavior, not a bug this phase should fix.
    CHECK( fdb.aux_data->valid_min == doctest::Approx(-5.0f * 3.0f + 7.0f) );
    CHECK( fdb.aux_data->valid_max == doctest::Approx(5.0f * 3.0f + 7.0f) );
}

TEST_CASE("netcdf_fill_aux_data: valid_min/valid_max come back unmodified with no scale_factor/add_offset") {
    // The companion case to the test above: with neither scale_factor nor
    // add_offset set, valid_min/valid_max are read back exactly as written.
    FillValueFile f( std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                      /*valid_min=*/-5.0f, /*valid_max=*/5.0f );

    FDBlist fdb;
    fdb.filename = f.path;
    fdb.aux_data = std::make_unique<NetCDFOptions>();
    netcdf_fill_aux_data( f.fileid, (char *)"data", &fdb );

    CHECK( fdb.aux_data->valid_min_set );
    CHECK( fdb.aux_data->valid_min == doctest::Approx(-5.0f) );
    CHECK( fdb.aux_data->valid_max_set );
    CHECK( fdb.aux_data->valid_max == doctest::Approx(5.0f) );
    CHECK_FALSE( fdb.aux_data->scale_factor_set );
    CHECK_FALSE( fdb.aux_data->add_offset_set );
}

TEST_CASE("netcdf_fill_aux_data: an unrelated attribute present, but no scale/offset, leaves them unset") {
    // n_atts == 0 short-circuits netcdf_fill_aux_data() before it ever
    // checks for scale_factor/add_offset/valid_min/valid_max -- giving the
    // variable an unrelated attribute (units) forces it past that early
    // return, so this actually exercises the "checked, not found" path
    // for each of the four attributes rather than the early-exit path.
    FillValueFile f( std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                      std::nullopt, std::nullopt, /*add_unrelated_units_attr=*/true );

    FDBlist fdb;
    fdb.filename = f.path;
    fdb.aux_data = std::make_unique<NetCDFOptions>();
    netcdf_fill_aux_data( f.fileid, (char *)"data", &fdb );

    CHECK_FALSE( fdb.aux_data->scale_factor_set );
    CHECK_FALSE( fdb.aux_data->add_offset_set );
    CHECK_FALSE( fdb.aux_data->valid_min_set );
    CHECK_FALSE( fdb.aux_data->valid_max_set );
}

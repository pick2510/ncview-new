// Integration tests for core/src/file_netcdf.cc against a real, synthetic
// netCDF file (written and read back via the plain netCDF C API) -- this is
// the file-I/O boundary the rest of core's pure-logic tests deliberately
// don't exercise.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

namespace {

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

// Several netcdf_*() functions (e.g. netcdf_dim_name_to_id(),
// netcdf_dim_id_to_name()) internally call the dispatching fi_n_dims() --
// not netcdf_fi_n_dims() directly -- which checks core's own module-static
// file_type (file.cc) and exit()s if it was never set. determine_file_type()
// is the only way to set it from outside file.cc, and it needs a real file
// to probe, so this must run after the sample file exists but before
// opening it for real.
int open_sample_file(const std::string &path) {
    Stringlist *files = nullptr;
    stringlist_add_string(&files, const_cast<char *>(path.c_str()), nullptr, SLTYPE_NULL);
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
    CHECK(std::strcmp(dims->string, "time") == 0);
    // Stringlist::next is an AnyPtr (see ncview/anyptr.h), not a raw
    // Stringlist*, so it converts implicitly on assignment but doesn't
    // support chained -> -- hop through explicit locals instead.
    Stringlist *second = dims->next;
    REQUIRE(second != nullptr);
    CHECK(std::strcmp(second->string, "lat") == 0);
    Stringlist *third = second->next;
    REQUIRE(third != nullptr);
    CHECK(std::strcmp(third->string, "lon") == 0);
}

TEST_CASE("file_netcdf: dim name/id lookups round-trip") {
    SampleFile f;
    int lat_id = netcdf_dim_name_to_id(f.fileid, (char *)"temp", (char *)"lat");
    CHECK(lat_id == 1); // second dim of temp(time,lat,lon)

    char *name = netcdf_dim_id_to_name(f.fileid, (char *)"temp", lat_id);
    REQUIRE(name != nullptr);
    CHECK(std::strcmp(name, "lat") == 0);

    CHECK(netcdf_dim_name_to_id(f.fileid, (char *)"temp", (char *)"not_a_dim") == -1);
}

TEST_CASE("file_netcdf: var and dim units come back as written") {
    SampleFile f;
    CHECK(std::strcmp(netcdf_var_units(f.fileid, (char *)"temp"), "K") == 0);
    CHECK(std::strcmp(netcdf_dim_units(f.fileid, (char *)"lat"), "degrees_north") == 0);
    CHECK(std::strcmp(netcdf_long_var_name(f.fileid, (char *)"temp"), "temperature") == 0);
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

TEST_CASE("file_netcdf: a name with no matching dimvar reports no values") {
    SampleFile f;
    // netcdf_has_dim_values()'s notion of "dimvar" is purely name-based (a
    // variable named identically to the dim, per netCDF's own coordinate-
    // variable convention -- see netcdf_dimvar_id()), with no cross-check
    // that a same-named dimension actually exists. A name matching nothing
    // in the file at all must not crash, just report no dim values.
    CHECK(netcdf_has_dim_values(f.fileid, (char *)"nonexistent") == 0);
}

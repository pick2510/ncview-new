// Regression test for the heap-buffer-overflow found in view_data_edit()
// during Phase 0d's ASan sweep (see modernization.md's "Sanitizer findings"
// section): it allocated exactly n_entries char* slots but then wrote a
// terminating NULL at line_array[n_entries], one past the end. This test
// exercises view_data_edit() directly (its bug is entirely on the core
// side of the interface.h seam -- x_dataedit() is a no-op stub, see
// stub_interface.cc) so a plain build catches wrong values and the ASan/
// UBSan CI job (Phase 0d) catches the overflow itself if it ever returns.
#include <cstdlib>
#include <vector>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "test_udunits_helper.h"

extern View *view;
extern char **g_last_dataedit_lines;
extern int g_last_dataedit_nx;

TEST_CASE("view_data_edit: allocates exactly n_entries+1 slots and fills them correctly") {
    ensure_ncview_misc_initialized();

    const size_t nx = 4, ny = 3;
    std::vector<float> data = {
        0.f,  1.f,  2.f,  3.f,
        10.f, 11.f, 12.f, 13.f,
        20.f, 21.f, 22.f, 23.f,
    };

    NCVar var{};
    var.size = { ny, nx }; // dim 0 = y, dim 1 = x -- see PixelFixture in test_pixels.cc
    var.name = "test_var";

    View v{};
    v.variable   = &var;
    v.x_axis_id  = 1;
    v.y_axis_id  = 0;
    v.data       = data;

    view = &v;
    options.invert_physical = true; // so row j in line_array matches row j in `data` directly

    g_last_dataedit_lines = nullptr;
    g_last_dataedit_nx    = 0;

    view_data_edit();

    REQUIRE(g_last_dataedit_lines != nullptr);
    CHECK(g_last_dataedit_nx == (int)nx);

    // Every one of the nx*ny data values must have been formatted, in
    // row-major (x fastest) order, with no overflow past the last one.
    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            size_t idx = j * nx + i;
            REQUIRE(g_last_dataedit_lines[idx] != nullptr);
            CHECK(std::strtof(g_last_dataedit_lines[idx], nullptr) == doctest::Approx(data[idx]));
        }
    }
    // The terminating NULL this test is guarding: previously written one
    // slot past the malloc'd buffer (a heap-buffer-overflow under ASan).
    CHECK(g_last_dataedit_lines[nx * ny] == nullptr);

    for (size_t k = 0; k < nx * ny; k++) free(g_last_dataedit_lines[k]);
    free(g_last_dataedit_lines);
    g_last_dataedit_lines = nullptr;
    view = nullptr;
}

// Copyright (C) 2026 Dominik Strebel
//
// Direct tests of FrameRenderer (core/src/frame_renderer.cc), extracted
// from data_to_pixels()'s pixel-mapping loop -- OOP_redesign plan, Step 2.
// tests/test_pixels.cc already exercises this loop indirectly through the
// full data_to_pixels() pipeline (View/NCVar/blowup and all), and those
// tests still pass unchanged after the extraction -- the real regression
// net for "did the extraction change any arithmetic". This file instead
// covers what test_pixels.cc's identity-pixel_transform,
// invert_physical==false fixture never could: invert_physical==true (the
// row-flip branch) and a non-identity pixel_transform table under
// display_type==PseudoColor (the remap branch at the end of the loop).
#include <vector>

#include <doctest/doctest.h>

#include "ncview/frame_renderer.h"

namespace {

PixelMapSettings make_settings(Transform t, bool invert_colors, bool invert_physical,
                                int n_colors, int n_extra_colors, int display_type) {
    return PixelMapSettings{ t, invert_colors, invert_physical, n_colors, n_extra_colors, display_type };
}

} // namespace

TEST_CASE("FrameRenderer::render matches data_to_pixels()'s known-exact grid (sanity/direct-call form)") {
    // Same 3x3 grid and expectation as test_pixels.cc's first TEST_CASE,
    // called directly instead of through View/NCVar/data_to_pixels().
    std::vector<float> grid = {
        0, 1, 2,
        3, -999, 5,
        6, 7, 8,
    };
    std::vector<ncv_pixel> pixel_transform(90);
    for (size_t i = 0; i < pixel_transform.size(); i++) pixel_transform[i] = (ncv_pixel)i;

    auto settings = make_settings(Transform::None, false, /*invert_physical=*/false, 80, 10, /*display_type=*/0);
    std::vector<ncv_pixel> pix(9);
    FrameRenderer::render(grid.data(), 3, 3, /*fill_value=*/-999, /*user_min=*/0, /*user_max=*/8,
                           settings, pixel_transform, pix.data());

    std::vector<ncv_pixel> expected = {
        70, 80, 89,
        40,  0, 60,
        10, 20, 30,
    };
    CHECK(pix == expected);
}

TEST_CASE("FrameRenderer::render: invert_physical=true skips the row flip") {
    // Same grid/settings as above except invert_physical=true -- row 0 of
    // the output should now be row 0 of the input (rawdata {0,1,2}), not
    // the last row, unlike every existing test_pixels.cc case (all of
    // which leave invert_physical at its default, false).
    std::vector<float> grid = {
        0, 1, 2,
        3, -999, 5,
        6, 7, 8,
    };
    std::vector<ncv_pixel> pixel_transform(90);
    for (size_t i = 0; i < pixel_transform.size(); i++) pixel_transform[i] = (ncv_pixel)i;

    auto settings = make_settings(Transform::None, false, /*invert_physical=*/true, 80, 10, 0);
    std::vector<ncv_pixel> pix(9);
    FrameRenderer::render(grid.data(), 3, 3, -999, 0, 8, settings, pixel_transform, pix.data());

    std::vector<ncv_pixel> expected = {
        10, 20, 30,   // rawdata 0,1,2 -- straight copy, no flip
        40,  0, 60,   // rawdata 3,MISSING,5
        70, 80, 89,   // rawdata 6,7,8
    };
    CHECK(pix == expected);
}

TEST_CASE("FrameRenderer::render: PseudoColor display_type remaps every pixel through pixel_transform") {
    // A single-cell field so the remap is unambiguous: rawdata==user_min
    // maps to color index n_extra_colors (10 here, since data clips to 0),
    // and a non-identity pixel_transform must be visible in the output --
    // every existing test uses an identity table, so this branch
    // (util.cc's old `if (options.display_type == PseudoColor)
    // pix_val = pixel_transform[pix_val];`) was previously unexercised.
    std::vector<float> grid = { 0.0f };
    std::vector<ncv_pixel> pixel_transform(20, 0);
    pixel_transform[10] = 200; // non-identity: index 10 maps to 200, not 10

    auto settings = make_settings(Transform::None, false, false, 5, 10, /*display_type=*/PseudoColor);
    std::vector<ncv_pixel> pix(1);
    FrameRenderer::render(grid.data(), 1, 1, /*fill_value=*/-999, /*user_min=*/0, /*user_max=*/1,
                           settings, pixel_transform, pix.data());

    CHECK(pix[0] == 200);
}

TEST_CASE("FrameRenderer::render: a missing value always maps to pixel_transform[0], regardless of display_type") {
    std::vector<float> grid = { -999.0f };
    std::vector<ncv_pixel> pixel_transform(20, 0);
    pixel_transform[0] = 42; // distinctive, so this test fails loudly if the missing-value path breaks

    auto settings = make_settings(Transform::None, false, false, 5, 10, PseudoColor);
    std::vector<ncv_pixel> pix(1);
    FrameRenderer::render(grid.data(), 1, 1, /*fill_value=*/-999, /*user_min=*/0, /*user_max=*/1,
                           settings, pixel_transform, pix.data());

    CHECK(pix[0] == 42);
}

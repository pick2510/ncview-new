// Copyright (C) 2026 Dominik Strebel
//
// Direct tests of FrameCache (core/src/frame_cache.cc), extracted from the
// bare FrameStore struct -- OOP_redesign plan, Step 3. Mirrors the field
// manipulations previously done by hand in view.cc's view_draw(),
// view_check_new_data(), init_saveframes(), and invalidate_all_saveframes().
#include <vector>

#include <doctest/doctest.h>

#include "ncview/frame_cache.h"

TEST_CASE("FrameCache starts invalid with zero geometry") {
    FrameCache cache;
    CHECK_FALSE(cache.valid());
    CHECK(cache.nt() == 0);
    CHECK(cache.lookup(0) == nullptr);
}

TEST_CASE("FrameCache::reset allocates storage and becomes valid") {
    FrameCache cache;
    cache.reset(/*nt=*/3, /*nx=*/2, /*ny=*/2);
    CHECK(cache.valid());
    CHECK(cache.nt() == 3);
    CHECK(cache.nx() == 2);
    CHECK(cache.ny() == 2);
    // Freshly reset: every frame is allocated but not yet marked valid.
    CHECK(cache.lookup(0) == nullptr);
    CHECK(cache.lookup(2) == nullptr);
}

TEST_CASE("FrameCache::store then lookup round-trips pixel data") {
    FrameCache cache;
    cache.reset(2, 2, 2);
    std::vector<ncv_pixel> pixels = {10, 20, 30, 40};
    cache.store(1, pixels.data(), pixels.size());

    CHECK(cache.lookup(0) == nullptr); // frame 0 untouched
    const ncv_pixel *got = cache.lookup(1);
    REQUIRE(got != nullptr);
    for (size_t i = 0; i < pixels.size(); i++)
        CHECK(got[i] == pixels[i]);
}

TEST_CASE("FrameCache::invalidateAll clears every stored frame but keeps storage") {
    FrameCache cache;
    cache.reset(2, 2, 2);
    std::vector<ncv_pixel> pixels = {1, 2, 3, 4};
    cache.store(0, pixels.data(), pixels.size());
    cache.store(1, pixels.data(), pixels.size());
    REQUIRE(cache.lookup(0) != nullptr);
    REQUIRE(cache.lookup(1) != nullptr);

    cache.invalidateAll();

    CHECK(cache.valid()); // still valid -- just nothing is cached right now
    CHECK(cache.lookup(0) == nullptr);
    CHECK(cache.lookup(1) == nullptr);
}

TEST_CASE("FrameCache::growTo preserves existing frames and adds invalid new ones") {
    FrameCache cache;
    cache.reset(2, 2, 2);
    std::vector<ncv_pixel> pixels = {5, 6, 7, 8};
    cache.store(0, pixels.data(), pixels.size());

    cache.growTo(5);

    CHECK(cache.nt() == 5);
    const ncv_pixel *frame0 = cache.lookup(0);
    REQUIRE(frame0 != nullptr);
    for (size_t i = 0; i < pixels.size(); i++)
        CHECK(frame0[i] == pixels[i]);
    CHECK(cache.lookup(1) == nullptr);
    CHECK(cache.lookup(4) == nullptr); // one of the newly-added frames
}

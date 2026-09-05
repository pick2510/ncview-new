// M1 checkpoint: proves ncview_core (a) links cleanly against nothing but
// the headless stub_interface.cc implementation of ncview/interface.h (no
// UI, no X11 -- see tests/CMakeLists.txt, which whole-archive-links
// ncview_core so any missing symbol anywhere in it fails here), and
// (b) produces correct results for a sample of pure, global-state-free
// core logic.
#include <cassert>
#include <cstdio>
#include <cstring>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "ncview/calcalcs.h"

static void test_clip() {
    // Note: upstream declares clip_i() in protos.h but never defines or
    // calls it anywhere -- it's dead. Only clip_f() actually exists.
    float f = 5.0f;
    clip_f(&f, 0.0f, 1.0f);
    assert(f == 1.0f);
}

static void test_close_enough() {
    assert(close_enough(1.0f, 1.0f) != 0);
    assert(close_enough(1.0f, 2.0f) == 0);
}

static void test_strncmp_nocase() {
    assert(strncmp_nocase((char*)"Longitude", (char*)"longitude", 9) == 0);
    assert(strncmp_nocase((char*)"Longitude", (char*)"latitude", 4) != 0);
}

static void test_groupnames() {
    char groupname[1024];
    int idx_slash = unpack_groupname((char*)"/forecast/temp", -1, groupname);
    assert(idx_slash >= 0);
    assert(std::strcmp(groupname, "/forecast") == 0);

    char sans_groups[1024], group[1024];
    varname_no_groups((char*)"/forecast/temp", sans_groups, group);
    assert(std::strcmp(sans_groups, "temp") == 0);
}

static void test_stringlist() {
    Stringlist *sl = nullptr;
    assert(stringlist_add_string(&sl, (char*)"one", nullptr, SLTYPE_NULL) == 0);
    assert(stringlist_add_string(&sl, (char*)"two", nullptr, SLTYPE_NULL) == 0);
    assert(n_strings_in_list(sl) == 2);
    stringlist_delete_entire_list(sl);
}

static void test_udunits2_end_to_end() {
    // M2 checkpoint: the vendored, statically-linked UDUNITS-2 initializes
    // (via its own $UDUNITS2_XML_PATH / installed-prefix logic, falling
    // back to NCVIEW_BUILD_TREE_UDUNITS2_XML -- see udu.cc:udu_utinit) and
    // can actually parse real unit strings from its database.
    udu_utinit(nullptr);
    assert(udu_utistime((char*)"time", (char*)"days since 2000-01-01") == 1);
    assert(udu_utistime((char*)"x", (char*)"meters") == 0);
}

static void test_calcalcs_standard_calendar() {
    calcalcs_cal *cal = ccs_init_calendar("standard");
    assert(cal != nullptr);

    int jday;
    assert(ccs_date2jday(cal, 2024, 3, 1, &jday) == 0);
    int y, m, d;
    assert(ccs_jday2date(cal, jday, &y, &m, &d) == 0);
    assert(y == 2024 && m == 3 && d == 1);

    int leap;
    assert(ccs_isleap(cal, 2024, &leap) == 0);
    assert(leap == 1);
    assert(ccs_isleap(cal, 2023, &leap) == 0);
    assert(leap == 0);

    ccs_free_calendar(cal);
}

int main() {
    test_clip();
    test_close_enough();
    test_strncmp_nocase();
    test_groupnames();
    test_stringlist();
    test_udunits2_end_to_end();
    test_calcalcs_standard_calendar();
    std::printf("core_headless_test: all checks passed\n");
    return 0;
}

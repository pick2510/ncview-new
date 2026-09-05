// Unit tests for core/src/calcalcs.cc (calendar arithmetic) and
// core/src/udu.cc (UDUNITS-2 glue) -- the date/time logic ncview relies on
// to interpret a variable's time dimension.
#include <cstring>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "ncview/calcalcs.h"

TEST_CASE("calendar: standard calendar jday round trip") {
    calcalcs_cal *cal = ccs_init_calendar("standard");
    REQUIRE(cal != nullptr);

    int jday;
    CHECK(ccs_date2jday(cal, 2024, 3, 1, &jday) == 0);
    int y, m, d;
    CHECK(ccs_jday2date(cal, jday, &y, &m, &d) == 0);
    CHECK(y == 2024);
    CHECK(m == 3);
    CHECK(d == 1);

    ccs_free_calendar(cal);
}

TEST_CASE("calendar: standard calendar leap years") {
    calcalcs_cal *cal = ccs_init_calendar("standard");
    REQUIRE(cal != nullptr);

    int leap;
    CHECK(ccs_isleap(cal, 2024, &leap) == 0);
    CHECK(leap == 1); // divisible by 4, not a century
    CHECK(ccs_isleap(cal, 2023, &leap) == 0);
    CHECK(leap == 0);
    CHECK(ccs_isleap(cal, 1900, &leap) == 0);
    CHECK(leap == 0); // century, not divisible by 400
    CHECK(ccs_isleap(cal, 2000, &leap) == 0);
    CHECK(leap == 1); // century, divisible by 400

    ccs_free_calendar(cal);
}

TEST_CASE("calendar: 360_day calendar has no leap years and fixed month lengths") {
    calcalcs_cal *cal = ccs_init_calendar("360_day");
    REQUIRE(cal != nullptr);

    int leap;
    CHECK(ccs_isleap(cal, 2000, &leap) == 0);
    CHECK(leap == 0);

    int dpm;
    for (int month = 1; month <= 12; month++) {
        CAPTURE(month);
        CHECK(ccs_dpm(cal, 2001, month, &dpm) == 0);
        CHECK(dpm == 30);
    }

    ccs_free_calendar(cal);
}

TEST_CASE("calendar: 365_day calendar never has Feb 29") {
    calcalcs_cal *cal = ccs_init_calendar("365_day");
    REQUIRE(cal != nullptr);

    int dpm;
    CHECK(ccs_dpm(cal, 2000, 2, &dpm) == 0); // otherwise-leap year
    CHECK(dpm == 28);

    ccs_free_calendar(cal);
}

TEST_CASE("calendar: day-of-year round trip") {
    calcalcs_cal *cal = ccs_init_calendar("standard");
    REQUIRE(cal != nullptr);

    int doy;
    CHECK(ccs_date2doy(cal, 2024, 3, 1, &doy) == 0);
    CHECK(doy == 61); // 2024 is a leap year: 31 (Jan) + 29 (Feb) + 1

    int m, d;
    CHECK(ccs_doy2date(cal, 2024, doy, &m, &d) == 0);
    CHECK(m == 3);
    CHECK(d == 1);

    ccs_free_calendar(cal);
}

TEST_CASE("calendar: dayssince advances a date by n days") {
    calcalcs_cal *cal = ccs_init_calendar("standard");
    REQUIRE(cal != nullptr);

    int y, m, d;
    CHECK(ccs_dayssince(cal, 2000, 1, 1, 1, cal, &y, &m, &d) == 0);
    CHECK(y == 2000);
    CHECK(m == 1);
    CHECK(d == 2);

    // 2000 is a leap year (366 days), so 366 days after Jan 1 lands
    // exactly on the following Jan 1.
    CHECK(ccs_dayssince(cal, 2000, 1, 1, 366, cal, &y, &m, &d) == 0);
    CHECK(y == 2001);
    CHECK(m == 1);
    CHECK(d == 1);

    ccs_free_calendar(cal);
}

TEST_CASE("calendar: invalid calendar name returns null") {
    calcalcs_cal *cal = ccs_init_calendar("not_a_real_calendar");
    CHECK(cal == nullptr);
}

TEST_CASE("calendar: invalid date is rejected") {
    calcalcs_cal *cal = ccs_init_calendar("standard");
    REQUIRE(cal != nullptr);

    int jday;
    CHECK(ccs_date2jday(cal, 2023, 2, 30, &jday) != 0); // Feb never has 30 days

    ccs_free_calendar(cal);
}

TEST_CASE("calendar: udunits2 recognizes time and non-time units") {
    udu_utinit(nullptr);
    CHECK(udu_utistime((char *)"time", (char *)"days since 2000-01-01") == 1);
    CHECK(udu_utistime((char *)"x", (char *)"meters") == 0);
    CHECK(udu_utistime((char *)"whatever_the_name", (char *)"seconds since 1970-01-01 00:00:00") == 1);
    // Garbage units strings are not time units either -- must not crash.
    CHECK(udu_utistime((char *)"y", (char *)"not a real unit string at all") == 0);
}

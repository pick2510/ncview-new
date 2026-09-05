// initialize_misc() (core/src/ncview.cc) -- upstream's own one-time,
// process-lifetime setup routine, which ncview_main() calls exactly once --
// itself calls udu_utinit(NULL) unconditionally. udu_utinit() replaces the
// shared `unitsys` UDUNITS-2 handle outright on every call, with no attempt
// to free or reconcile the old one, and several functions built on top of
// it (udu_utistime()'s "time_unit_with_origin", udu_fmt_time()'s
// "dataunits") cache a ut_unit parsed against whichever `unitsys` was live
// the first time they ran. A ut_unit from one ut_system compared against
// one from another fails outright ("ut_are_convertible(): Units in
// different unit-systems") -- confirmed the hard way: two independent
// per-file one-time-init guards (one calling initialize_misc(), another
// calling udu_utinit() directly) each fire once, for two calls total
// across the test binary, and the second silently breaks every
// udu_utistime()/udu_fmt_time() call made afterward.
//
// `inline` here isn't a hint: it makes this function's `static done` a
// single, ODR-shared variable across every translation unit that includes
// this header, so the whole ncview_core_tests binary runs
// initialize_misc() -- and therefore udu_utinit() -- exactly once,
// regardless of which test files need it, in what order, or whether they
// also need other initialize_misc() side effects (default Options values,
// framestore reset) that a udu_utinit()-only call wouldn't provide.
#pragma once

#include "ncview/protos.h"

inline void ensure_ncview_misc_initialized() {
    static bool done = false;
    if (!done) {
        initialize_misc();
        done = true;
    }
}

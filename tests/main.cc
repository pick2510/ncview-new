// Custom main() (DOCTEST_CONFIG_IMPLEMENT, not _WITH_MAIN) so the process
// can exit immediately after doctest reports its result. Every other
// tests/test_*.cc file just #includes doctest.h (without either define) and
// registers its TEST_CASEs into the same global registry -- doctest links
// all of it into one binary and one aggregate report.
//
// The _Exit() below isn't a style choice: confirmed on Windows CI that this
// suite's doctest run completes and prints "Status: SUCCESS!" (all 27 cases,
// 282 assertions) and then the *process* hangs afterwards -- CTest reports
// a Timeout despite every test having actually passed. HDF5 (netCDF-4's
// storage backend) registers atexit-time library cleanup that's a known
// source of slow/hung process exit; a normal `return` runs that plus every
// static destructor before the process can end. _Exit() skips all of it,
// which is safe here since the tests themselves already released everything
// they opened (each SampleFile closes its netCDF handle and removes its
// temp file in its own destructor, well before this point).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdlib>

int main(int argc, char **argv) {
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    int res = context.run();
    if (context.shouldExit()) return res;
    std::_Exit(res);
}

// Single translation unit that instantiates doctest's own main(). Every
// other tests/test_*.cc file just #includes doctest.h (without this define)
// and registers its TEST_CASEs into the same global registry -- doctest
// links all of it into one binary and one aggregate report.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

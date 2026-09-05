#pragma once

#include <cstdlib>
#if defined(_WIN32)
#include <windows.h>
#endif

// Terminates the process without running static-destructor/atexit teardown
// -- HDF5's own library-cleanup hooks among them, which on Windows CI have
// been confirmed (doctest reports every test passing, then the process
// itself never returns) to hang. std::_Exit() does NOT avoid this on
// Windows: it still routes through ExitProcess(), which -- unlike
// TerminateProcess() -- notifies every loaded DLL via
// DllMain(DLL_PROCESS_DETACH) before actually terminating, and that is
// exactly where the hang happens (under the loader lock). TerminateProcess()
// skips DLL_PROCESS_DETACH entirely, which is what actually routes around
// it. The test process's own state doesn't need to survive past this call,
// so skipping teardown is safe here even though it wouldn't be in the
// shipped app.
inline void ncviewTestsFastExit(int code) {
#if defined(_WIN32)
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
#else
    std::_Exit(code);
#endif
}

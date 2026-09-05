// The real ncview entry point. Everything else -- reading the state file,
// parsing args, opening the netCDF files, building the display, and running
// the event loop -- happens in ncview_main() (core/src/ncview.cc), which
// drives the UI purely through the ncview/interface.h seam that ncview_ui
// implements (ui/src/interface_fltk.cc, ui/src/main_window.cc).
#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

int main( int argc, char **argv )
{
	return ncview_main( argc, argv );
}

/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 2026 Dominik Strebel
 * Copyright (C) 1993 through 2024  David W. Pierce
 *
 * This program  is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as 
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License, version 3, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * davidwilliampierce@gmail.com
 */

/*******************************************************************************
 * 	util.c
 *
 *	utility routines for ncview
 *
 *	should be independent of both the user interface and the data
 * 	file format.
 *******************************************************************************/

/* This memory check makes things run slow */
/* define CHECK_MEM */

#include <vector>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "ncview/frame_renderer.h"

#include "math.h"

/*-------------------*/
#ifdef HAVE_UDUNITS2
#include "udunits2.h"
extern ut_system *unitsys;
#endif 
/*-------------------*/

extern Options   options;

/******************************************************************************
 * Allocate space for a NetCDFOptions structure. `new`, not malloc(): the
 * one caller (Dataset::new_fdblist(), dataset.cc) immediately hands the
 * result to a std::unique_ptr<NetCDFOptions>, whose default deleter calls
 * `delete`, not `free()`. This was a real malloc/delete mismatch --
 * caught by ASan's alloc-dealloc-mismatch check only once something
 * actually destroyed a Dataset mid-process (tests/support/
 * session_fixture.h's SessionFixture, "refine the architecture" plan's
 * Phase 0a): every previous test run left the global Dataset's teardown
 * to process exit, but tests/main.cc's ncviewTestsFastExit() calls
 * std::_Exit()/TerminateProcess() specifically to skip static destructors
 * (see fast_exit.h), so the mismatched delete had never actually run.
 */
	void
new_netcdf( NetCDFOptions **n )
{
	(*n) = new NetCDFOptions();
	(*n)->valid_range_set  = false;
	(*n)->valid_min_set    = false;
	(*n)->valid_max_set    = false;
	(*n)->scale_factor_set = false;
	(*n)->add_offset_set   = false;

	(*n)->valid_range[0] = 0.0;
	(*n)->valid_range[1] = 0.0;
	(*n)->valid_min      = 0.0;
	(*n)->valid_max      = 0.0;
	(*n)->scale_factor   = 1.0;
	(*n)->add_offset     = 0.0;
}



/******************************************************************************
 * Set the style of blowup we want to do.
 */
	void
set_blowup_type( BlowupType new_type )
{
	if( new_type == BlowupType::Replicate ) 
		in_set_label( Label::BlowupType, "Repl"   );
	else
		in_set_label( Label::BlowupType, "Bi-lin" );

	options.blowup_type = new_type;
}

/*****************************************************************************
 * Indicate an error condition which can be continued from. Routed through
 * in_dialog (a real ncview_ui responsibility) rather than being one itself.
 * Formerly interface_glue.cc, dissolved into this file since core's other
 * error-reporting callers already live here.
 */
void
in_error( const char *message )
{
	in_dialog( message, false );
}


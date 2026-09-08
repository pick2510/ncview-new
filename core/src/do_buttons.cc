/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 2026 Dominik Strebel
 * Copyright (C) 1993 through 2024 David W. Pierce
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
 * David W. Pierce
 * davidwilliampierce@gmail.com
 */

/*************************************************************************
 * The 21 do_*() actions (plus which_button_pressed(), in_button_pressed(),
 * in_colormap_selected()) that used to be implemented here now live as
 * named methods on ViewerController (ncview/viewer_controller.h,
 * viewer_controller.cc) -- OOP_redesign plan, Step 7. Every function below
 * is a one-line forward onto the single global ViewerController instance,
 * kept under their original free-function names/signatures so every
 * existing call site elsewhere in core/, ui/, and tests/ (do_pause() from
 * view.cc, do_range()/do_options()/do_dimset() from interface_fltk.cc's
 * NCVIEW_TEST_DIALOG hook, in_button_pressed() from util.cc/ui/, the
 * do_range()/do_fastforward()/do_pause()/which_button_pressed() calls in
 * tests/test_controller_characterization.cc, ...) keeps compiling and
 * behaving identically.
 *************************************************************************/

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "ncview/viewer_controller.h"

	Button
which_button_pressed( void )
{
	return( g_viewer_controller.whichButtonPressed() );
}

	void
do_range( Modifier modifier )
{
	g_viewer_controller.range( modifier );
}

	void
do_dimset( Modifier modifier )
{
	g_viewer_controller.dimset( modifier );
}

	void
do_restart( Modifier modifier )
{
	g_viewer_controller.restart( modifier );
}

	void
do_rewind( Modifier modifier )
{
	g_viewer_controller.rewind( modifier );
}

	void
do_quit( Modifier modifier )
{
	g_viewer_controller.quit( modifier );
}

	void
do_backwards( Modifier modifier )
{
	g_viewer_controller.backwards( modifier );
}

	void
do_pause( Modifier modifier )
{
	g_viewer_controller.pause( modifier );
}

	void
do_forward( Modifier modifier )
{
	g_viewer_controller.forward( modifier );
}

	void
do_fastforward( Modifier modifier )
{
	g_viewer_controller.fastforward( modifier );
}

	void
do_colormap_sel( Modifier modifier )
{
	g_viewer_controller.colormapSelect( modifier );
}

	void
do_invert_physical( Modifier modifier )
{
	g_viewer_controller.invertPhysical( modifier );
}

	void
do_data_edit( Modifier modifier )
{
	g_viewer_controller.dataEdit( modifier );
}

	void
do_invert_colormap( Modifier modifier )
{
	g_viewer_controller.invertColormap( modifier );
}

	void
do_set_minimum( Modifier modifier )
{
	g_viewer_controller.setMinimum( modifier );
}

	void
do_set_maximum( Modifier modifier )
{
	g_viewer_controller.setMaximum( modifier );
}

	void
do_blowup( Modifier modifier )
{
	g_viewer_controller.blowup( modifier );
}

	void
do_transform( Modifier modifier )
{
	g_viewer_controller.transform( modifier );
}

	void
do_blowup_type( Modifier modifier )
{
	g_viewer_controller.blowupType( modifier );
}

	void
do_info( Modifier modifier )
{
	g_viewer_controller.info( modifier );
}

	void
do_options( Modifier modifier )
{
	g_viewer_controller.optionsDialog( modifier );
}

/*****************************************************************************
 * Called when a button is pressed (by the UI's widget callbacks, or by
 * core itself -- e.g. util.cc pauses playback on an error by calling
 * do_pause() directly).  Argument 'button_id' indicates which button was
 * pressed.  Argument modifier should ideally take on one of 4 values:
 * Modifier::M1, Modifier::M2, Modifier::M3, and Modifier::M4, used in a
 * generalized sense to mean "normal action", "accelerated action",
 * "backwards action", and "accelerated backwards action". If these are
 * not available, just always use Modifier::M1.
 */
void
in_button_pressed( Button button_id, Modifier modifier )
{
	g_viewer_controller.dispatch( button_id, modifier );
}

/*****************************************************************************
 * Vector through this routine when a colormap has been picked directly (by
 * name) from the UI's colormap combobox -- the direct-pick counterpart of
 * do_colormap_sel()'s cycle-by-one-step BUTTON_COLORMAP_SELECT handling
 * above, which this deliberately mirrors (same in_install_..() then
 * view_draw()/view_recompute_colorbar() tail) so a picked colormap repaints
 * exactly like a cycled one does.
 */
void
in_colormap_selected( const char *name )
{
	g_viewer_controller.colormapSelectByName( name );
}

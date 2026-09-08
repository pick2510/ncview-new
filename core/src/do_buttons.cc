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
 * Routines to handle a button being pressed.  My convention for the 
 * modifiers: Modifier::M1 means the standard action.  Modifier::M2 means an accelerated 
 * version of the standard aciton. Modifier::M3 means a backwards version of the 
 * standard action.  Modifier::M4 means an accelerated backwards version of the
 * standard action.
 *************************************************************************/

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

#define DELAY_DELTA	350.0
#define DELAY_OFFSET	10L

extern Options options;

static Button cur_button = Button::Pause;

/*===========================================================================================*/
	Button
which_button_pressed( void )
{
	return( cur_button );
}

/*===========================================================================================*/
	void
do_range( Modifier modifier )
{
	init_saveframes();
	if( modifier == Modifier::M3 )
		view_set_range_frame();
	else
		view_set_range();
}

/*===========================================================================================*/
	void
do_dimset( Modifier modifier )
{
	view_set_scan_dims();
}

/*===========================================================================================*/
	void
do_restart( Modifier modifier )
{
	cur_button = Button::Pause;

	in_timer_clear();

	set_scan_view( 0 );
	view_draw    ( true, false );

	in_timer_clear();
}

/*===========================================================================================*/
	void
do_rewind( Modifier modifier )
{
	unsigned long delay_millisec;
	size_t	size;
	double	d_delta;
	int	i_delta;

	cur_button = Button::Rewind;

	delay_millisec = (long)(DELAY_DELTA * options.frame_delay) + DELAY_OFFSET;
 
	in_timer_clear();

	if( modifier == Modifier::M2 ) {
		size = view_current_nt();
		d_delta = (double)size / 1000.0;
		if( d_delta < 10.0 )
			i_delta = -10;
		else
			i_delta = -d_delta;
		change_view( i_delta, FRAMES );
		in_timer_set( [](){ do_rewind(Modifier::M2); }, delay_millisec );
		}
	else
		{
		change_view( -1, FRAMES );
		in_timer_set( [](){ do_rewind(Modifier::M1); }, delay_millisec );
		}
}

/*===========================================================================================*/
	void
do_quit( Modifier modifier )
{
	quit_app();
}

/*===========================================================================================*/
	void
do_backwards( Modifier modifier )
{
	size_t	size;

	in_timer_clear();

	if( modifier == Modifier::M2 ) {
		size = view_current_nt();
		if( size < 500 ) 
			change_view( -10, PERCENT );
		else if( size < 5000 ) 
			change_view(  -5, PERCENT );
		else if( size < 50000 ) 
			change_view(  -2, PERCENT );
		else
			change_view(  -1, PERCENT );
		}
	else
		change_view( -1, FRAMES );

	cur_button = Button::Pause;
}

/*===========================================================================================*/
	void
do_pause( Modifier modifier )
{
	cur_button = Button::Pause;
	in_timer_clear();
}

/*===========================================================================================*/
	void
do_forward( Modifier modifier )
{
	size_t	size;

	cur_button = Button::Pause;
	in_timer_clear();

	if( modifier == Modifier::M2 ) {
		size = view_current_nt();
		if( size < 500 ) 
			change_view( 10, PERCENT );
		else if( size < 5000 ) 
			change_view(  5, PERCENT );
		else if( size < 50000 ) 
			change_view(  2, PERCENT );
		else
			change_view(  1, PERCENT );
		}
	else
		change_view( 1, FRAMES );
}

/*===========================================================================================*/
	void
do_fastforward( Modifier modifier )
{
	unsigned long	delay_millisec;
	size_t	size;
	double	d_delta;
	int	i_delta;

	cur_button = Button::Fastforward;

	in_timer_clear();

	delay_millisec = (long)(DELAY_DELTA * options.frame_delay) + DELAY_OFFSET;

	if( modifier == Modifier::M2 ) {
		size = view_current_nt();
		d_delta = (double)size / 1000.0;
		if( d_delta < 10.0 )
			i_delta = 10;
		else
			i_delta = d_delta;
		if( change_view( i_delta, FRAMES ) == 0 )
			in_timer_set( [](){ do_fastforward(Modifier::M2); }, delay_millisec );
		}
	else
		{
		if( change_view( 1, FRAMES ) == 0 )
			in_timer_set( [](){ do_fastforward(Modifier::M1); }, delay_millisec );
		}
}
		
/*===========================================================================================*/
	void
do_colormap_sel( Modifier modifier )
{
	if( modifier == Modifier::M3 )
		in_install_prev_colormap( true );
	else
		in_install_next_colormap( true );
	view_draw( true, false );
	view_recompute_colorbar();
}

/*===========================================================================================*/
	void
do_invert_physical( Modifier modifier )
{
	init_saveframes();
	if( options.invert_physical )
		options.invert_physical = false;
	else
		options.invert_physical = true;
	view_draw( true, false );
	redraw_dimension_info();
}

/*===========================================================================================*/
	void
do_data_edit( Modifier modifier )
{
/* do_overlay(); */
	view_data_edit();
}

/*===========================================================================================*/
	void
do_invert_colormap( Modifier modifier )
{
	init_saveframes();
	if( options.invert_colors )
		options.invert_colors = false;
	else
		options.invert_colors = true;
	view_draw( true, false );
	view_recompute_colorbar();
}

/*===========================================================================================*/
	void
do_set_minimum( Modifier modifier )
{
}

/*===========================================================================================*/
	void
do_set_maximum( Modifier modifier )
{
}

/*===========================================================================================*/
	void
do_blowup( Modifier modifier )
{
	int view_var_is_valid = true;

	if( modifier == Modifier::M3 )
		view_change_blowup( -1, true, view_var_is_valid );

	else if( modifier == Modifier::M2 ) {
		/* Double the current blowup -- make image BIGGER */
		if( options.blowup > 0 )
			view_change_blowup( options.blowup, true, view_var_is_valid );
		else	
			view_change_blowup( -(options.blowup)/2, true, view_var_is_valid );
		}

	else if( modifier == Modifier::M4 ) {
		/* Halve the current blowup -- make image SMALLER */
		if( options.blowup > 0 ) 
			view_change_blowup( -(options.blowup/2), true, view_var_is_valid );
		else
			view_change_blowup( options.blowup, true, view_var_is_valid );
		}
		
	else
		view_change_blowup( 1, true, view_var_is_valid );
	
	/* If we are shrinking magnification, then try re-saving
	 * the frames because now there might be enough room.
	 */
	init_saveframes();
	if( modifier == Modifier::M3 )
		options.save_frames = true;
}

/*===========================================================================================*/
	void
do_transform( Modifier modifier )
{
	init_saveframes();
	if( modifier == Modifier::M3 )
		view_change_transform( -1 );
	else
		view_change_transform( 1 );
}

/*===========================================================================================*/
	void
do_blowup_type( Modifier modifier )
{
	init_saveframes();
	if( options.blowup_type == BlowupType::Replicate )
		set_blowup_type( BlowupType::Bilinear );
	else
		set_blowup_type( BlowupType::Replicate );
	view_draw( true, false );
}

/*===========================================================================================*/
	void
do_info( Modifier modifier )
{
	view_information();
}

/*===========================================================================================*/
	void
do_options( Modifier modifier )
{
	set_options();
}

/*****************************************************************************
 * Called when a button is pressed (by the UI's widget callbacks, or by
 * core itself -- e.g. util.cc pauses playback on an error by calling
 * in_button_pressed(BUTTON_PAUSE, Modifier::M1)).  Argument 'button_id' indicates
 * which button was pressed.  Argument modifier should ideally take on one
 * of 4 values: Modifier::M1, Modifier::M2, Modifier::M3, and Modifier::M4, used in a generalized sense
 * to mean "normal action", "accelerated action", "backwards action", and
 * "accelerated backwards action". If these are not available, just always
 * use Modifier::M1.
 *
 * Formerly interface_glue.cc, dissolved into this file since it's a 1:1
 * dispatch onto the do_*() functions defined here.
 */
void
in_button_pressed( Button button_id, Modifier modifier )
{
	switch( button_id ) {
		case Button::Range:
			do_range( modifier );
			break;

		case Button::Dimset:
			do_dimset( modifier );
			break;

		case Button::Transform:
			do_transform( modifier );
			break;

		case Button::Blowup:
			do_blowup( modifier );
			break;

		case Button::Quit:
			do_quit( modifier );
			break;

		case Button::Restart:
			do_restart( modifier );
			break;

		case Button::Rewind:
			do_rewind( modifier );
			break;

		case Button::Backwards:
			do_backwards( modifier );
			break;

		case Button::Pause:
			do_pause( modifier );
			break;

		case Button::Forward:
			do_forward( modifier );
			break;

		case Button::Fastforward:
			do_fastforward( modifier );
			break;

		case Button::ColormapSelect:
			do_colormap_sel( modifier );
			break;

		case Button::InvertPhysical:
			do_invert_physical( modifier );
			break;

		case Button::InvertColormap:
			do_invert_colormap( modifier );
			break;

		case Button::Minimum:
			do_set_minimum( modifier );
			break;

		case Button::Maximum:
			do_set_maximum( modifier );
			break;

		case Button::BlowupType:
			do_blowup_type( modifier );
			break;

		case Button::Edit:
			do_data_edit( modifier );
			break;

		case Button::Info:
			do_info( modifier );
			break;

		case Button::Print:
			do_print();
			break;

		case Button::Options:
			do_options( modifier );
			break;

		default:
			fprintf( stderr, "in_button_pressed: unknown " );
			fprintf( stderr, "button id: %d\n", static_cast<int>(button_id) );
			exit( -1 );
		}
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
	in_install_colormap_by_name( name, true );
	view_draw( true, false );
	view_recompute_colorbar();
}

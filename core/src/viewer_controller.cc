/*
 * core/src/viewer_controller.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * See ncview/viewer_controller.h. Bodies moved verbatim from do_buttons.cc's
 * do_*() functions (OOP_redesign plan, Step 7) -- cur_button is now a
 * private member (cur_button_) instead of a file-static, and the two
 * timer-rearming lambdas (rewind, fastforward) capture [this] instead of
 * recursing through a free function.
 */
#include "ncview/viewer_controller.h"

#include "ncview/includes.h"
#include "ncview/protos.h"

#define DELAY_DELTA	350.0
#define DELAY_OFFSET	10L

extern Options options;

ViewerController g_viewer_controller;

/*===========================================================================================*/
void
ViewerController::dispatch( Button button_id, Modifier modifier )
{
	switch( button_id ) {
		case Button::Range:		range( modifier );		break;
		case Button::Dimset:		dimset( modifier );		break;
		case Button::Transform:		transform( modifier );		break;
		case Button::Blowup:		blowup( modifier );		break;
		case Button::Quit:		quit( modifier );		break;
		case Button::Restart:		restart( modifier );		break;
		case Button::Rewind:		rewind( modifier );		break;
		case Button::Backwards:		backwards( modifier );		break;
		case Button::Pause:		pause( modifier );		break;
		case Button::Forward:		forward( modifier );		break;
		case Button::Fastforward:	fastforward( modifier );	break;
		case Button::ColormapSelect:	colormapSelect( modifier );	break;
		case Button::InvertPhysical:	invertPhysical( modifier );	break;
		case Button::InvertColormap:	invertColormap( modifier );	break;
		case Button::Minimum:		setMinimum( modifier );	break;
		case Button::Maximum:		setMaximum( modifier );	break;
		case Button::BlowupType:	blowupType( modifier );	break;
		case Button::Edit:		dataEdit( modifier );		break;
		case Button::Info:		info( modifier );		break;
		case Button::Print:		print();			break;
		case Button::Options:		optionsDialog( modifier );	break;

		default:
			fprintf( stderr, "ViewerController::dispatch: unknown " );
			fprintf( stderr, "button id: %d\n", static_cast<int>(button_id) );
			exit( -1 );
		}
}

/*===========================================================================================*/
void
ViewerController::range( Modifier modifier )
{
	init_saveframes();
	if( modifier == Modifier::M3 )
		view_set_range_frame();
	else
		view_set_range();
}

/*===========================================================================================*/
void
ViewerController::dimset( Modifier modifier )
{
	view_set_scan_dims();
}

/*===========================================================================================*/
void
ViewerController::restart( Modifier modifier )
{
	cur_button_ = Button::Pause;

	in_timer_clear();

	set_scan_view( 0 );
	view_draw    ( true, false );

	in_timer_clear();
}

/*===========================================================================================*/
void
ViewerController::rewind( Modifier modifier )
{
	unsigned long delay_millisec;
	size_t	size;
	double	d_delta;
	int	i_delta;

	cur_button_ = Button::Rewind;

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
		in_timer_set( [this](){ rewind(Modifier::M2); }, delay_millisec );
		}
	else
		{
		change_view( -1, FRAMES );
		in_timer_set( [this](){ rewind(Modifier::M1); }, delay_millisec );
		}
}

/*===========================================================================================*/
void
ViewerController::quit( Modifier modifier )
{
	quit_app();
}

/*===========================================================================================*/
void
ViewerController::backwards( Modifier modifier )
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

	cur_button_ = Button::Pause;
}

/*===========================================================================================*/
void
ViewerController::pause( Modifier modifier )
{
	cur_button_ = Button::Pause;
	in_timer_clear();
}

/*===========================================================================================*/
void
ViewerController::forward( Modifier modifier )
{
	size_t	size;

	cur_button_ = Button::Pause;
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
ViewerController::fastforward( Modifier modifier )
{
	unsigned long	delay_millisec;
	size_t	size;
	double	d_delta;
	int	i_delta;

	cur_button_ = Button::Fastforward;

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
			in_timer_set( [this](){ fastforward(Modifier::M2); }, delay_millisec );
		}
	else
		{
		if( change_view( 1, FRAMES ) == 0 )
			in_timer_set( [this](){ fastforward(Modifier::M1); }, delay_millisec );
		}
}

/*===========================================================================================*/
void
ViewerController::colormapSelect( Modifier modifier )
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
ViewerController::colormapSelectByName( const char *name )
{
	in_install_colormap_by_name( name, true );
	view_draw( true, false );
	view_recompute_colorbar();
}

/*===========================================================================================*/
void
ViewerController::invertPhysical( Modifier modifier )
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
ViewerController::dataEdit( Modifier modifier )
{
	view_data_edit();
}

/*===========================================================================================*/
void
ViewerController::invertColormap( Modifier modifier )
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
ViewerController::setMinimum( Modifier modifier )
{
}

/*===========================================================================================*/
void
ViewerController::setMaximum( Modifier modifier )
{
}

/*===========================================================================================*/
void
ViewerController::blowup( Modifier modifier )
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
ViewerController::transform( Modifier modifier )
{
	init_saveframes();
	if( modifier == Modifier::M3 )
		view_change_transform( -1 );
	else
		view_change_transform( 1 );
}

/*===========================================================================================*/
void
ViewerController::blowupType( Modifier modifier )
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
ViewerController::info( Modifier modifier )
{
	view_information();
}

/*===========================================================================================*/
void
ViewerController::optionsDialog( Modifier modifier )
{
	set_options();
}

/*===========================================================================================*/
void
ViewerController::print()
{
	do_print();
}

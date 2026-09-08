/*
 * core/src/viewer_ui_bridge.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * See ncview/viewer_ui.h. OOP_redesign plan, Step 9b. Defines every free
 * function ncview/interface.h declares (core's toolkit seam) as a one-line
 * forwarder onto the currently-installed ViewerUi (g_app.ui). This is
 * what lets every existing core call site (view.cc, util.cc, do_print.cc,
 * overlay.cc, ncview.cc, viewer_controller.cc) keep calling these
 * functions by their original free-function names, unchanged, while the
 * actual implementation now lives behind a swappable virtual interface
 * (FltkViewerUi for the real app, a recording fake for tests) instead of
 * requiring a second, parallel set of ~48 free-function definitions per
 * binary.
 */
#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/app_context.h"

void 	in_display_stuff	( const char *s, const char *var_name )
{
	g_app.ui->in_display_stuff( s, var_name );
}

void 	in_set_edit_place	( size_t index, int x, int y, int nx, int ny )
{
	g_app.ui->in_set_edit_place( index, x, y, nx, ny );
}

void 	in_indicate_active_var  ( const char *var_name )
{
	g_app.ui->in_indicate_active_var( var_name );
}

void 	in_indicate_active_dim  ( Dimension dimension, const char *dim_name )
{
	g_app.ui->in_indicate_active_dim( dimension, dim_name );
}

void 	in_parse_args		( int *p_argc, char **argv )
{
	g_app.ui->in_parse_args( p_argc, argv );
}

void 	in_initialize		( void )
{
	g_app.ui->in_initialize();
}

void 	in_set_label		( Label label_id, const char *string )
{
	g_app.ui->in_set_label( label_id, string );
}

void	in_process_user_input	( void )
{
	g_app.ui->in_process_user_input();
}

void	in_draw_2d_field 	( const unsigned char *data, size_t width, size_t height, size_t timestep )
{
	g_app.ui->in_draw_2d_field( data, width, height, timestep );
}

void	in_create_colormap	( const char *name, const ncv_pixel r[256], const ncv_pixel g[256], const ncv_pixel b[256] )
{
	g_app.ui->in_create_colormap( name, r, g, b );
}

char	*in_install_next_colormap( int do_widgets_flag )
{
	return g_app.ui->in_install_next_colormap( do_widgets_flag );
}

int	in_set_2d_size   	( size_t width, size_t height )
{
	return g_app.ui->in_set_2d_size( width, height );
}

void	in_set_sensitive	( Button button_id, int state )
{
	g_app.ui->in_set_sensitive( button_id, state );
}

Message	in_dialog		( const char *message, int want_cancel_button )
{
	return g_app.ui->in_dialog( message, want_cancel_button );
}

void 	in_var_set_sensitive	( const char *var_name, int sensitivity )
{
	g_app.ui->in_var_set_sensitive( var_name, sensitivity );
}

void 	in_fill_dim_info	( const NCDim *d, int please_flip )
{
	g_app.ui->in_fill_dim_info( d, please_flip );
}

void	in_set_cur_dim_value	( const char *name, const char *string )
{
	g_app.ui->in_set_cur_dim_value( name, string );
}

void 	in_set_cursor_busy	( void )
{
	g_app.ui->in_set_cursor_busy();
}

void 	in_set_cursor_normal	( void )
{
	g_app.ui->in_set_cursor_normal();
}

int 	in_set_scan_dims	( const Stringlist *dim_list, const char *x_axis, const char *y_axis, Stringlist **new_dim_list )
{
	return g_app.ui->in_set_scan_dims( dim_list, x_axis, y_axis, new_dim_list );
}

void	in_change_min		( const char *label )
{
	g_app.ui->in_change_min( label );
}

void 	in_flush		( void )
{
	g_app.ui->in_flush();
}

int	in_popup_XY_graph	( size_t n, int dimindex, double *xvals, double *yvals, const char *x_axis_title,
				const char *y_axis_title, const char *title, const char *legend,
				const Stringlist *scannable_dims )
{
	return g_app.ui->in_popup_XY_graph( n, dimindex, xvals, yvals, x_axis_title,
			y_axis_title, title, legend, scannable_dims );
}

void 	in_query_pointer_position( int *x, int *y )
{
	g_app.ui->in_query_pointer_position( x, y );
}

void	in_popup_2d_window	( void )
{
	g_app.ui->in_popup_2d_window();
}

void	in_popdown_2d_window	( void )
{
	g_app.ui->in_popdown_2d_window();
}

void 	in_timer_clear		( void )
{
	g_app.ui->in_timer_clear();
}

int	in_report_auto_overlay  ( void )
{
	return g_app.ui->in_report_auto_overlay();
}

void 	in_timer_set            ( std::function<void()> callback, unsigned long delay_millisec )
{
	g_app.ui->in_timer_set( callback, delay_millisec );
}

char    *in_install_prev_colormap( int do_widgets )
{
	return g_app.ui->in_install_prev_colormap( do_widgets );
}

char	*in_install_colormap_by_name( const char *name, int do_widgets )
{
	return g_app.ui->in_install_colormap_by_name( name, do_widgets );
}

Stringlist *in_choose_input_files( void )
{
	return g_app.ui->in_choose_input_files();
}

Message in_choose_save_file( const char *title, const char *default_name, char *ret_path, size_t ret_path_size )
{
	return g_app.ui->in_choose_save_file( title, default_name, ret_path, ret_path_size );
}

void	in_print		( const PrintInfo &info, const PrintOptions &po )
{
	g_app.ui->in_print( info, po );
}

void	set_options		( void )
{
	g_app.ui->set_options();
}

Message	printer_options		( PrintOptions *po )
{
	return g_app.ui->printer_options( po );
}

Message x_range( float old_min, float old_max, float global_min, float global_max,
		float *new_min, float *new_max, int *allvars )
{
	return g_app.ui->x_range( old_min, old_max, global_min, global_max, new_min, new_max, allvars );
}

void	x_dataedit( char **text, int nx )
{
	g_app.ui->x_dataedit( text, nx );
}

int	x_seen_colormap_name( const char *name )
{
	return g_app.ui->x_seen_colormap_name( name );
}

void	x_check_legal_colormap_loaded( void )
{
	g_app.ui->x_check_legal_colormap_loaded();
}

void	x_create_colorbar( float user_min, float user_max, Transform transform )
{
	g_app.ui->x_create_colorbar( user_min, user_max, transform );
}

void	x_draw_colorbar( void )
{
	g_app.ui->x_draw_colorbar();
}

void	x_error( const char *message )
{
	g_app.ui->x_error( message );
}

void	x_force_set_invert_state( int state )
{
	g_app.ui->x_force_set_invert_state( state );
}

void	x_init_dim_info( const Stringlist *dim_list )
{
	g_app.ui->x_init_dim_info( dim_list );
}

void	x_set_var_sensitivity( const char *varname, int sens )
{
	g_app.ui->x_set_var_sensitivity( varname, sens );
}

void	unlock_plot( void )
{
	g_app.ui->unlock_plot();
}

Stringlist *get_persistent_X_state( void )
{
	return g_app.ui->get_persistent_X_state();
}

void	pix_to_rgb( ncv_pixel pix, int *r, int *g, int *b )
{
	g_app.ui->pix_to_rgb( pix, r, g, b );
}

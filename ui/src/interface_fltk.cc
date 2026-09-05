/*
 * ui/src/interface_fltk.cc
 *
 * The free-function side of the ncview/interface.h toolkit seam: every
 * function ncview_core calls (in_*) or that core calls directly by name
 * (set_options, x_range, ...) is implemented here, delegating to the
 * MainWindow singleton (ui/include/ncview_ui/main_window.h). This is the
 * FLTK replacement for upstream's src/interface/interface.c +
 * src/interface/x_interface.c.
 */
#include <cstdio>
#include <cstring>

#include <FL/Fl.H>
#include <FL/fl_ask.H>

#include "ncview_ui/main_window.h"

using ncview_ui::MainWindow;
using ncview_ui::instance;

/* ---- lifecycle / event loop ------------------------------------------- */

void in_parse_args( int *p_argc, char **argv )
{
	// FLTK consumes its own -display/-geometry etc. via Fl::args() if
	// ever needed; ncview's own option parser (parse_options(), M4) runs
	// separately over what's left. Nothing FLTK-specific to strip yet.
	(void)p_argc; (void)argv;
}

void in_initialize( void )
{
	// Upstream read this (and similar) from an X application-defaults
	// resource ("Ncview*blowupDefaultSize: 300", fallback_resources.h);
	// it's plain core state (view.cc:calculate_blowup divides by it),
	// not something routed through a seam function, so ncview_ui just
	// sets it directly. Leaving it at its zero-initialized default is a
	// real bug, not a graceful default: calculate_blowup() divides by
	// it unconditionally, producing a divide-by-zero -> inf -> UB
	// float-to-int conversion (observed as options.blowup becoming
	// INT_MIN on this machine).
	options.blowup_default_size = 300;
	options.delta_step = 1; // unused anywhere in core today, set for fidelity

	// By this point ncview_main() has already run initialize_file_interface(),
	// so the global `variables` list is populated.
	instance()->populateVarList();
	instance()->window()->show();
	if( getenv( "NCVIEW_TEST_AUTOSELECT" ) && variables )
		in_variable_selected( variables->name );
}

void in_process_user_input( void )
{
	// Upstream's contract: never returns, loops handling UI events.
	Fl::run();
}

void in_flush( void )
{
	Fl::flush();
}

void in_set_cursor_busy( void )   { instance()->setCursorBusy( true ); }
void in_set_cursor_normal( void ) { instance()->setCursorBusy( false ); }

/* ---- timers ------------------------------------------------------------ */

namespace {
// ncview only ever has one outstanding animation tick at a time (rewind,
// fastforward, or the new-data-check timer), so a single slot is enough to
// let in_timer_clear() actually free a cancelled callback's closure instead
// of leaking it.
std::function<void()> *g_pending_timer = nullptr;

void timerTrampoline( void *data )
{
	auto *fn = static_cast<std::function<void()>*>( data );
	if( g_pending_timer == fn ) g_pending_timer = nullptr;
	(*fn)();
	delete fn;
}
} // namespace

void in_timer_clear( void )
{
	Fl::remove_timeout( timerTrampoline );
	delete g_pending_timer;
	g_pending_timer = nullptr;
}

void in_timer_set( std::function<void()> callback, unsigned long delay_millisec )
{
	in_timer_clear();
	auto *fn = new std::function<void()>( std::move( callback ) );
	g_pending_timer = fn;
	Fl::add_timeout( delay_millisec / 1000.0, timerTrampoline, fn );
}

/* ---- labels / sensitivity / dim buttons -------------------------------- */

void in_set_label( int label_id, char *string )
{
	if( string == nullptr ) return;
	instance()->setLabel( label_id, string );
}

void in_set_sensitive( int button_id, int state )
{
	instance()->setSensitive( button_id, state );
}

void in_var_set_sensitive( char *var_name, int sensitivity )
{
	x_set_var_sensitivity( var_name, sensitivity );
}

void in_indicate_active_var( char *var_name )
{
	instance()->indicateActiveVar( var_name );
}

void in_indicate_active_dim( int dimension, char *dim_name )
{
	instance()->indicateActiveDim( dimension, dim_name );
}

void in_fill_dim_info( NCDim *d, int please_flip )
{
	instance()->fillDimInfo( d, please_flip );
}

void in_set_cur_dim_value( char *name, char *string )
{
	instance()->setCurDimValue( name, string );
}

/* ---- 2-D field / colormap ------------------------------------------------ */

void in_draw_2d_field( unsigned char *data, size_t width, size_t height, size_t timestep )
{
	instance()->draw2DField( data, width, height, timestep );
}

void in_create_colormap( char *name, ncv_pixel r[256], ncv_pixel g[256], ncv_pixel b[256] )
{
	instance()->createColormap( name, r, g, b );
}

char *in_install_next_colormap( int do_widgets )
{
	return instance()->installNextColormap( do_widgets );
}

char *in_install_prev_colormap( int do_widgets )
{
	return instance()->installPrevColormap( do_widgets );
}

int in_set_2d_size( size_t width, size_t height )
{
	int r = instance()->set2DSize( width, height );
	in_flush();
	// Upstream relies on growing the image widget generating an X11
	// "expose" event that its Xt event loop wires back to change_view()
	// (see view.cc:set_scan_variable, the comment above its
	// in_set_2d_size() call: "an expansion generates an expose event...
	// registered to call change_view"). FLTK has no equivalent wiring in
	// this port, so without this the very first frame of a newly
	// selected variable never actually gets drawn.
	if( r >= 1 ) change_view( 0, FRAMES );
	return r;
}

/* ---- pointer / mouse ---------------------------------------------------- */

void in_query_pointer_position( int *x, int *y )
{
	if( x ) *x = Fl::event_x();
	if( y ) *y = Fl::event_y();
}

/* ---- dialogs / errors ---------------------------------------------------- */

int in_dialog( char *message, char *ret_string, int want_cancel_button )
{
	if( ret_string != nullptr ) {
		const char *result = fl_input( "%s", ret_string, message );
		if( result == nullptr ) return MESSAGE_CANCEL;
		std::strncpy( ret_string, result, STRINGLIST_MAX_LEN-1 );
		return MESSAGE_OK;
	}
	if( want_cancel_button ) {
		int r = fl_choice( "%s", "Cancel", "OK", nullptr, message );
		return r == 1 ? MESSAGE_OK : MESSAGE_CANCEL;
	}
	fl_alert( "%s", message );
	return MESSAGE_OK;
}

void x_error( char *message )
{
	fl_alert( "%s", message ? message : "(unknown error)" );
}

/* ---- popups not yet implemented (M4) ------------------------------------- */

void in_display_stuff( char *s, char *var_name )
{
	(void)s; (void)var_name;
}

void in_set_edit_place( size_t index, int x, int y, int nx, int ny )
{
	(void)index; (void)x; (void)y; (void)nx; (void)ny;
}

int in_set_scan_dims( Stringlist *dim_list, char *x_axis_name, char *y_axis_name, Stringlist **new_dim_list )
{
	// TODO(M4): a real dialog (upstream's range.c-adjacent x_set_scan_dims).
	// For now: keep whatever the caller proposed, changing nothing.
	(void)dim_list; (void)x_axis_name; (void)y_axis_name;
	if( new_dim_list ) *new_dim_list = nullptr;
	return 0;
}

void in_change_min( char *label )
{
	(void)label;
}

int in_popup_XY_graph( size_t n, int dimindex, double *xvals, double *yvals, char *x_axis_title,
	char *y_axis_title, char *title, char *legend, Stringlist *scannable_dims )
{
	// TODO(M4): real plot window (ui/src/plot_window.cc, per PORTING.md).
	(void)n; (void)dimindex; (void)xvals; (void)yvals; (void)x_axis_title;
	(void)y_axis_title; (void)title; (void)legend; (void)scannable_dims;
	fl_alert( "XY plots are not implemented yet." );
	return 0;
}

void in_popup_2d_window( void )   {}
void in_popdown_2d_window( void ) {}

int in_report_auto_overlay( void ) { return 0; }

/* ---- extra seam: real UI dialogs/state (M4 stubs for now) ---------------- */

void set_options( void )
{
	fl_alert( "Options dialog is not implemented yet." );
}

int printer_options( PrintOptions * )
{
	fl_alert( "Print options dialog is not implemented yet." );
	return MESSAGE_CANCEL;
}

void printer_options_init( void ) {}

int x_range( float old_min, float old_max, float, float, float *new_min, float *new_max, int *allvars )
{
	// TODO(M4): real range dialog. For now, leave the range unchanged.
	*new_min = old_min;
	*new_max = old_max;
	if( allvars ) *allvars = 0;
	return MESSAGE_CANCEL;
}

void x_dataedit( char **text, int nx )
{
	(void)text; (void)nx;
	fl_alert( "Data editing is not implemented yet." );
}

int x_seen_colormap_name( char *name )
{
	return instance()->seenColormapName( name ) ? 1 : 0;
}

void x_check_legal_colormap_loaded( void )
{
	instance()->checkLegalColormapLoaded();
}

void x_create_colorbar( float user_min, float user_max, int transform )
{
	instance()->createColorbar( user_min, user_max, transform );
}

void x_draw_colorbar( void )
{
	instance()->drawColorbar();
}

void x_force_set_invert_state( int state )
{
	(void)state;
}

void x_init_dim_info( Stringlist *dim_list )
{
	instance()->makeDimButtons( dim_list );
}

void x_set_var_sensitivity( char *varname, int sens )
{
	(void)varname; (void)sens;
}

void unlock_plot( void ) {}

Stringlist *get_persistent_X_state( void )
{
	return nullptr;
}

void pix_to_rgb( ncv_pixel pix, int *r, int *g, int *b )
{
	// Best-effort: without direct access to MainWindow's private
	// colormap table this just echoes the index; only do_print.cc's
	// (not yet wired, M5) PostScript path depends on getting real RGB
	// here. TODO(M5): expose the active colormap for this.
	if( r ) *r = pix;
	if( g ) *g = pix;
	if( b ) *b = pix;
}

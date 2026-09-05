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
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Table.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include "ncview_ui/main_window.h"
#include "ncview_ui/plot_window.h"

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
	if( const char *sel = getenv( "NCVIEW_TEST_AUTOSELECT" ) ) {
		NCVar *v = variables;
		// A specific variable name may be given (besides "1", meaning "just
		// pick the first one"); useful for driving a chosen 2-D field in
		// headless/manual testing without a real mouse.
		if( std::strcmp( sel, "1" ) != 0 )
			for( NCVar *c = variables; c != nullptr; c = (NCVar *)c->next )
				if( std::strcmp( c->name, sel ) == 0 ) { v = c; break; }
		if( v != nullptr )
			in_variable_selected( v->name );
	}
	if( const char *d = getenv( "NCVIEW_TEST_DIALOG" ) ) {
		if( std::strcmp( d, "range" ) == 0 ) do_range( MOD_1 );
		else if( std::strcmp( d, "options" ) == 0 ) do_options( MOD_1 );
		else if( std::strcmp( d, "dimset" ) == 0 ) do_dimset( MOD_1 );
		else if( std::strcmp( d, "info" ) == 0 ) view_information();
		else if( std::strcmp( d, "dataedit" ) == 0 ) view_data_edit();
		else if( std::strcmp( d, "plot" ) == 0 ) plot_XY();
		else if( std::strcmp( d, "overlay" ) == 0 ) do_overlay( OVERLAY_P8DEG, nullptr, FALSE );
		else if( std::strcmp( d, "print" ) == 0 ) {
			// do_print() reads the printopts defaults that ncview_main()
			// sets up via print_init() -- which runs *after* in_initialize()
			// returns (see ncview.cc). Defer to the first event-loop tick so
			// the manual "print" test hook sees the same state a real
			// button press would.
			Fl::add_timeout( 0.0, []( void * ) { do_print(); } );
		}
	}
	if( const char *b = getenv( "NCVIEW_TEST_BUTTON" ) ) {
		// Drives any button through the exact same in_button_pressed() path
		// MainWindow::buttonCallback uses for a real click -- for
		// regression-checking buttons (blowup, transform, invert, ...)
		// under Xvfb without a real mouse. Deferred one tick for the same
		// reason "print" is: some of these (BUTTON_BLOWUP, BUTTON_RESTART)
		// interact with state that in_initialize()'s caller (ncview_main())
		// only finishes setting up after in_initialize() returns.
		static const struct { const char *name; int id; } kButtons[] = {
			{ "rewind", BUTTON_REWIND }, { "backwards", BUTTON_BACKWARDS },
			{ "pause", BUTTON_PAUSE }, { "forward", BUTTON_FORWARD },
			{ "fastforward", BUTTON_FASTFORWARD }, { "colormap", BUTTON_COLORMAP_SELECT },
			{ "invert_physical", BUTTON_INVERT_PHYSICAL }, { "invert_colormap", BUTTON_INVERT_COLORMAP },
			{ "minimum", BUTTON_MINIMUM }, { "maximum", BUTTON_MAXIMUM },
			{ "blowup", BUTTON_BLOWUP }, { "restart", BUTTON_RESTART },
			{ "transform", BUTTON_TRANSFORM }, { "blowup_type", BUTTON_BLOWUP_TYPE },
		};
		for( const auto &e : kButtons )
			if( std::strcmp( b, e.name ) == 0 ) {
				int id = e.id;
				Fl::add_timeout( 0.0, [](void *data) { in_button_pressed( (int)(intptr_t)data, MOD_1 ); },
					(void*)(intptr_t)id );
				break;
			}
	}
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

namespace {
// M5: "-frames" (options.dump_frames) dumps every displayed frame to a PNG,
// e.g. to assemble into a movie. Upstream's x_interface.c did this itself
// with a direct libpng call inside its x_draw_2d_field(); FLTK already
// bundles libpng for its own image support (fltk_images/fl_write_png.cxx),
// so this needs no new dependency -- just an RGB expansion via pix_to_rgb,
// which do_print.cc's PostScript writer already relies on.
void dumpFrameToPng( const unsigned char *data, size_t width, size_t height, size_t frameno )
{
	static bool error_state = false;
	if( error_state ) return;

	char filename[64];
	snprintf( filename, sizeof(filename), "frame.%05zu.png", frameno );

	std::vector<unsigned char> rgb( width * height * 3 );
	for( size_t i = 0; i < width * height; i++ ) {
		int r, g, b;
		pix_to_rgb( data[i], &r, &g, &b );
		rgb[i*3+0] = (unsigned char)(r >> 8);
		rgb[i*3+1] = (unsigned char)(g >> 8);
		rgb[i*3+2] = (unsigned char)(b >> 8);
	}
	if( fl_write_png( filename, rgb.data(), (int)width, (int)height, 3 ) != 0 ) {
		fprintf( stderr, "ncview: can't write PNG file %s\n", filename );
		error_state = true;
	}
}
} // namespace

void in_draw_2d_field( unsigned char *data, size_t width, size_t height, size_t timestep )
{
	if( options.dump_frames )
		dumpFrameToPng( data, width, height, timestep );
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
	// Must return the same data-buffer-pixel coordinate space
	// view_report_position() gets (ImageView::screenToBuffer() undoes the
	// widget's position plus its zoom/pan/centering transform) -- core's
	// mouse_xy_to_data_xy() divides this straight through by options.blowup
	// with no further offset, so a raw Fl::event_x()/y() here (window-
	// relative, not widget-relative) would misplace every caller: plot_XY(),
	// set_min/max_from_curdata(), set_dataedit_place().
	instance()->queryPointerPosition( x, y );
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

/* ---- variable-info popup -------------------------------------------------- */

void in_display_stuff( char *s, char *var_name )
{
	char window_title[132];
	snprintf( window_title, sizeof(window_title), "Attributes of \"%s\"", var_name ? var_name : "" );

	// Owns itself: deleted when the user hits Close. Upstream capped these
	// at MAX_DISPLAY_POPUPS live popups (interface/display_info.c); FLTK has
	// no widget-count pressure that made that limit necessary, so any
	// number may be open at once here.
	auto *win = new Fl_Double_Window( 520, 260, window_title );
	win->begin();
	auto *buf = new Fl_Text_Buffer();
	buf->text( s ? s : "" );
	auto *disp = new Fl_Text_Display( 10, 10, 500, 200 );
	disp->buffer( buf );
	disp->wrap_mode( Fl_Text_Display::WRAP_AT_BOUNDS, 0 );
	auto *close_btn = new Fl_Button( 220, 220, 80, 30, "Close" );
	close_btn->callback( []( Fl_Widget *w, void * ) {
		Fl_Double_Window *window = static_cast<Fl_Double_Window*>( w->window() );
		window->hide();
		Fl::delete_widget( window );
	} );
	win->resizable( disp );
	win->end();
	win->show();
}

/* ---- data-edit grid -------------------------------------------------------- */

namespace {

// One data-edit window can be open at a time (matches upstream: x_dataedit()
// runs its own blocking mini event loop, so only one is ever live). The
// table cells are backed directly by the char** upstream hands us (each
// entry is a 32-byte buffer from view_data_edit()), so editing a cell just
// rewrites that buffer in place.
class DataEditTable : public Fl_Table {
public:
	DataEditTable( int x, int y, int w, int h, int nx, int ny, char **text )
		: Fl_Table( x, y, w, h ), nx_( nx ), text_( text )
	{
		rows( ny );
		cols( nx );
		row_header( 0 );
		col_header( 0 );
		row_height_all( 22 );
		col_width_all( 72 );
		end();
	}

	int nx() const { return nx_; }

protected:
	void draw_cell( TableContext context, int R, int C, int X, int Y, int W, int H ) override
	{
		if( context != CONTEXT_CELL ) return;
		int index = R * nx_ + C;
		fl_push_clip( X, Y, W, H );
		fl_color( FL_WHITE );
		fl_rectf( X, Y, W, H );
		fl_color( FL_BLACK );
		if( text_ && text_[index] )
			fl_draw( text_[index], X + 3, Y, W - 6, H, FL_ALIGN_LEFT );
		fl_rect( X, Y, W, H );
		fl_pop_clip();
	}

private:
	int nx_;
	char **text_;
};

DataEditTable *g_dataedit_table = nullptr;

void dataeditDoneCallback( Fl_Widget *w, void *data )
{
	*static_cast<bool*>( data ) = true;
	w->window()->hide();
}

void dataeditDumpCallback( Fl_Widget *, void * )
{
	view_data_edit_dump();
}

} // namespace

void x_dataedit( char **text, int nx )
{
	int n = 0;
	while( text[n] != nullptr ) n++;
	int ny = nx > 0 ? n / nx : 0;
	if( ny <= 0 ) return;

	Fl_Double_Window win( 520, 420, "Data Edit" );
	win.begin();
	DataEditTable table( 10, 10, 500, 350, nx, ny, text );
	table.when( FL_WHEN_RELEASE );
	auto *dump_btn = new Fl_Button( 10, 370, 100, 30, "Dump Data" );
	dump_btn->callback( dataeditDumpCallback );
	bool done = false;
	auto *done_btn = new Fl_Button( 410, 370, 100, 30, "Done" );
	done_btn->callback( dataeditDoneCallback, &done );
	win.end();

	// Double-click (or single release; Fl_Table doesn't distinguish well
	// without extra bookkeeping) on a cell prompts for a new value, mirroring
	// upstream's list-widget click -> x_dialog() -> in_change_dat() flow.
	table.callback( []( Fl_Widget *w, void * ) {
		auto *t = static_cast<DataEditTable*>( w );
		if( t->callback_context() != Fl_Table::CONTEXT_CELL || Fl::event() != FL_RELEASE ) return;
		int row = t->callback_row(), col = t->callback_col();
		int index = row * t->nx() + col;
		char **cells = static_cast<char**>( t->user_data() );
		if( cells == nullptr || cells[index] == nullptr ) return;

		char line[132];
		strncpy( line, cells[index], sizeof(line)-1 );
		line[sizeof(line)-1] = '\0';
		const char *result = fl_input( "Value:", line );
		if( result == nullptr ) return;

		float new_val, dummy;
		if( sscanf( result, "%f %f", &new_val, &dummy ) != 1 ) return;

		view_change_dat( (size_t)index, new_val );
		snprintf( cells[index], 32, "%-10.5g", new_val );
		t->redraw();
	} );
	// Fl_Widget::argument() stores its value as a plain `long`, which
	// truncates a pointer on Windows' LLP64 model (long stays 32-bit
	// there even in a 64-bit build); user_data() stores a real void*
	// with no such width loss, for the exact same "opaque callback
	// payload" purpose here.
	table.user_data( (void *)text );

	g_dataedit_table = &table;

	win.show();
	while( win.shown() ) Fl::wait();

	g_dataedit_table = nullptr;
}

void in_set_edit_place( size_t index, int x, int y, int nx, int ny )
{
	(void)x; (void)y; (void)ny;
	if( g_dataedit_table == nullptr || nx <= 0 ) return;
	int row = (int)(index / (size_t)nx);
	int col = (int)(index % (size_t)nx);
	g_dataedit_table->set_selection( row, col, row, col );
	g_dataedit_table->row_position( row );
	g_dataedit_table->col_position( col );
	g_dataedit_table->redraw();
}

int in_set_scan_dims( Stringlist *dim_list, char *x_axis_name, char *y_axis_name, Stringlist **new_dim_list )
{
	return instance()->scanDimsDialog( dim_list, x_axis_name, y_axis_name, new_dim_list );
}

void in_change_min( char *label )
{
	(void)label;
}

int in_popup_XY_graph( size_t n, int dimindex, double *xvals, double *yvals, char *x_axis_title,
	char *y_axis_title, char *title, char *legend, Stringlist *scannable_dims )
{
	if( scannable_dims == nullptr ) {
		in_error( (char *)"Internal error: got NULL scannable_dims in in_popup_XY_graph!" );
		return -1;
	}
	return ncview_ui::popupXYGraph( n, dimindex, xvals, yvals, x_axis_title, y_axis_title,
			title, legend, scannable_dims );
}

void in_popup_2d_window( void )   {}
void in_popdown_2d_window( void ) {}

int in_report_auto_overlay( void )
{
	// Upstream's x_report_auto_overlay() returns an X application-resource
	// default ("Ncview*autoOverlay", app_data.auto_overlay) that's ANDed
	// with the live options.auto_overlay toggle (view.cc:set_scan_variable)
	// -- effectively a second, resource-file-only on/off switch. That
	// resource defaults to 1 (DEFAULT_AUTO_OVERLAY in x_interface.c) and
	// this port has no application-resource system to override it with, so
	// returning 0 here (the previous M3 stub) silently disabled automatic
	// overlays entirely, no matter what the Options dialog's "Automatic
	// coastline overlay" checkbox said. Return the upstream default instead.
	return 1;
}

/* ---- extra seam: real UI dialogs/state (M4 stubs for now) ---------------- */

void set_options( void )
{
	instance()->setOptionsDialog();
}

int printer_options( PrintOptions *po )
{
	return instance()->printerOptionsDialog( po );
}

void printer_options_init( void ) {}

int x_range( float old_min, float old_max, float global_min, float global_max, float *new_min, float *new_max, int *allvars )
{
	return instance()->rangeDialog( old_min, old_max, global_min, global_max, new_min, new_max, allvars );
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

void unlock_plot( void ) { ncview_ui::unlockPlot(); }

Stringlist *get_persistent_X_state( void )
{
	return nullptr;
}

void pix_to_rgb( ncv_pixel pix, int *r, int *g, int *b )
{
	instance()->pixelToRgb( pix, r, g, b );
}

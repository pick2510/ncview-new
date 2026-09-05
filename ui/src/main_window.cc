#include "ncview_ui/main_window.h"

#include <cstdio>
#include <cstring>

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Float_Input.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Round_Button.H>

namespace ncview_ui {

namespace {
// Matches util.cc:data_to_pixels()'s pixel encoding: valid data occupies
// indices [10, 10+n_colors), everything below is reserved (missing/out of
// range). We don't have n_colors here, so just clamp to the array.
inline void lookup( const unsigned char *r, const unsigned char *g, const unsigned char *b,
                     unsigned char idx, unsigned char *out )
{
	out[0] = r[idx];
	out[1] = g[idx];
	out[2] = b[idx];
}
} // namespace

/* ===================== ImageView ===================== */

ImageView::ImageView( int x, int y, int w, int h ) : Fl_Widget( x, y, w, h )
{
	std::memset( colormap_r_, 128, sizeof(colormap_r_) );
	std::memset( colormap_g_, 128, sizeof(colormap_g_) );
	std::memset( colormap_b_, 128, sizeof(colormap_b_) );
}

ImageView::~ImageView() = default;

void ImageView::setColormap( const unsigned char *r, const unsigned char *g, const unsigned char *b )
{
	std::memcpy( colormap_r_, r, 256 );
	std::memcpy( colormap_g_, g, 256 );
	std::memcpy( colormap_b_, b, 256 );
	if( !pixels_.empty() ) {
		// Re-expand with the new colormap so a colormap change is
		// visible without waiting for the next frame.
		setData( pixels_.data(), width_, height_ );
	}
	redraw();
}

void ImageView::setData( const unsigned char *data, size_t width, size_t height )
{
	width_ = width;
	height_ = height;
	pixels_.assign( data, data + width*height );
	rgb_buf_.resize( width*height*3 );
	for( size_t i = 0; i < width*height; i++ )
		lookup( colormap_r_, colormap_g_, colormap_b_, pixels_[i], &rgb_buf_[i*3] );
	redraw();
}

void ImageView::draw()
{
	fl_color( FL_DARK2 );
	fl_rectf( x(), y(), w(), h() );
	if( rgb_buf_.empty() || width_ == 0 || height_ == 0 )
		return;

	// Center the image in the widget; ncview itself decides the pixel
	// size via the blowup factor (view.c), so width_/height_ already
	// reflect that -- we just draw at 1:1 and clip/center.
	int ox = x() + (w() - (int)width_) / 2;
	int oy = y() + (h() - (int)height_) / 2;
	fl_draw_image( rgb_buf_.data(), ox, oy, (int)width_, (int)height_, 3, 0 );
}

int ImageView::handle( int event )
{
	switch( event ) {
		case FL_PUSH:
		case FL_DRAG:
		case FL_MOVE: {
			unsigned int mask = 0;
			if( Fl::event_button1() ) mask |= 1;
			if( Fl::event_button2() ) mask |= 2;
			if( Fl::event_button3() ) mask |= 4;
			int ox = x() + (w() - (int)width_) / 2;
			int oy = y() + (h() - (int)height_) / 2;
			view_report_position( Fl::event_x() - ox, Fl::event_y() - oy, mask );
			// Middle-button press/drag highlights the corresponding cell in
			// the data-edit window, if one is open (matches upstream's
			// Btn2Up/Btn2Motion -> do_set_dataedit_place() translation).
			if( (event == FL_PUSH || event == FL_DRAG) && Fl::event_button2() )
				set_dataedit_place();
			return 1;
		}
		case FL_RELEASE:
			// Matches upstream's ccontour_widget translations: a plain
			// left-button release pops up an XY plot along the cursor's
			// position; the same with Ctrl held instead sets the current
			// min/max (Btn1 -> min, Btn3 -> max) from the value under the
			// cursor.
			if( Fl::event_button() == FL_LEFT_MOUSE ) {
				if( Fl::event_state( FL_CTRL ) ) set_min_from_curdata();
				else plot_XY();
			} else if( Fl::event_button() == FL_RIGHT_MOUSE && Fl::event_state( FL_CTRL ) ) {
				set_max_from_curdata();
			}
			return 1;
		default:
			return Fl_Widget::handle( event );
	}
}

/* ===================== Colorbar ===================== */

Colorbar::Colorbar( int x, int y, int w, int h ) : Fl_Widget( x, y, w, h )
{
	std::memset( colormap_r_, 128, sizeof(colormap_r_) );
	std::memset( colormap_g_, 128, sizeof(colormap_g_) );
	std::memset( colormap_b_, 128, sizeof(colormap_b_) );
}

void Colorbar::setColormap( const unsigned char *r, const unsigned char *g, const unsigned char *b )
{
	std::memcpy( colormap_r_, r, 256 );
	std::memcpy( colormap_g_, g, 256 );
	std::memcpy( colormap_b_, b, 256 );
	redraw();
}

void Colorbar::setRange( float user_min, float user_max, int transform )
{
	user_min_ = user_min;
	user_max_ = user_max;
	transform_ = transform;
	redraw();
}

void Colorbar::draw()
{
	int n_colors = options.n_colors > 0 ? options.n_colors : 200;
	for( int px = 0; px < w(); px++ ) {
		int idx = 10 + (px * n_colors) / (w() > 0 ? w() : 1);
		if( idx > 255 ) idx = 255;
		fl_color( fl_rgb_color( colormap_r_[idx], colormap_g_[idx], colormap_b_[idx] ) );
		fl_line( x()+px, y(), x()+px, y()+h() );
	}
	char buf[64];
	fl_color( FL_BLACK );
	fl_font( FL_HELVETICA, 10 );
	snprintf( buf, sizeof(buf), "%g", user_min_ );
	fl_draw( buf, x(), y()+h()+12 );
	snprintf( buf, sizeof(buf), "%g", user_max_ );
	fl_draw( buf, x()+w()-40, y()+h()+12 );
}

/* ===================== MainWindow ===================== */

// Lazily constructed: upstream calls in_create_colormap() (via
// initialize_colormaps()) *before* in_initialize() -- registering
// colormaps has to work before the window exists. Fl_Window/Fl_Widget
// construction itself doesn't touch the display (that happens on show()),
// so building the widgets early is safe.
MainWindow *instance()
{
	static MainWindow *w = new MainWindow();
	return w;
}

MainWindow::MainWindow()
{
	const int W = 900, H = 760;
	win_ = new Fl_Double_Window( W, H, "ncview" );

	labels_[LABEL_TITLE]        = new Fl_Box( 10, 5, W-20, 20 );
	labels_[LABEL_SCANVAR_NAME] = new Fl_Box( 10, 25, 200, 18 );
	labels_[LABEL_SCAN_PLACE]   = new Fl_Box( 220, 25, 200, 18 );
	labels_[LABEL_DATA_EXTREMA] = new Fl_Box( 10, 43, 300, 18 );
	labels_[LABEL_DATA_VALUE]   = new Fl_Box( 320, 43, 200, 18 );
	labels_[LABEL_COLORMAP_NAME]= new Fl_Box( 10, 61, 150, 18 );
	labels_[LABEL_BLOWUP]       = new Fl_Box( 170, 61, 80, 18 );
	labels_[LABEL_TRANSFORM]    = new Fl_Box( 260, 61, 80, 18 );
	labels_[LABEL_BLOWUP_TYPE]  = new Fl_Box( 350, 61, 100, 18 );
	labels_[LABEL_CCINFO_1]     = new Fl_Box( 460, 61, 200, 18 );
	labels_[LABEL_CCINFO_2]     = new Fl_Box( 460, 79, 200, 18 );
	labels_[LABEL_SKIP]         = new Fl_Box( 10, 79, 150, 18 );
	labels_[LABEL_SCALAR_DIMS]  = new Fl_Box( 170, 79, 280, 18 );
	for( auto *b : labels_ ) if( b ) { b->box( FL_NO_BOX ); b->align( FL_ALIGN_INSIDE | FL_ALIGN_LEFT ); }

	var_browser_ = new Fl_Hold_Browser( 10, 100, 180, H-220 );
	var_browser_->callback( &MainWindow::varBrowserCallback, this );

	image_ = new ImageView( 200, 100, W-210, H-320 );
	colorbar_ = new Colorbar( 200, H-210, W-210, 20 );

	dim_pack_ = new Fl_Pack( 10, H-180, W-20, 100 );
	dim_pack_->type( Fl_Pack::VERTICAL );

	button_bar_ = new Fl_Pack( 10, H-70, W-20, 60 );
	button_bar_->type( Fl_Pack::HORIZONTAL );
	button_bar_->spacing( 2 );
	rebuildButtonBar();

	win_->end();
	win_->resizable( image_ );
}

struct ButtonSpec { int id; const char *text; };
static const ButtonSpec kButtonSpecs[] = {
	{ BUTTON_REWIND, "@|<" }, { BUTTON_BACKWARDS, "@<" }, { BUTTON_PAUSE, "@||" },
	{ BUTTON_FORWARD, "@>" }, { BUTTON_FASTFORWARD, "@>|" }, { BUTTON_RESTART, "Restart" },
	{ BUTTON_COLORMAP_SELECT, "Colormap" }, { BUTTON_INVERT_PHYSICAL, "Inv.Phys" },
	{ BUTTON_INVERT_COLORMAP, "Inv.Cmap" }, { BUTTON_MINIMUM, "Min" }, { BUTTON_MAXIMUM, "Max" },
	{ BUTTON_BLOWUP, "Blowup" }, { BUTTON_BLOWUP_TYPE, "Bl.Type" }, { BUTTON_TRANSFORM, "Transform" },
	{ BUTTON_DIMSET, "DimSet" }, { BUTTON_RANGE, "Range" }, { BUTTON_EDIT, "Edit" },
	{ BUTTON_INFO, "Info" }, { BUTTON_PRINT, "Print" }, { BUTTON_OPTIONS, "Options" },
	{ BUTTON_QUIT, "Quit" },
};

void MainWindow::rebuildButtonBar()
{
	for( const auto &spec : kButtonSpecs ) {
		auto *btn = new Fl_Button( 0, 0, 60, 26, spec.text );
		btn->callback( &MainWindow::buttonCallback, (void*)(intptr_t)spec.id );
		button_bar_->add( btn );
		buttons_[spec.id] = btn;
	}
}

void MainWindow::buttonCallback( Fl_Widget *, void *data )
{
	int id = (int)(intptr_t)data;
	in_button_pressed( id, MOD_1 );
}

void MainWindow::varBrowserCallback( Fl_Widget *, void *data )
{
	auto *self = static_cast<MainWindow*>(data);
	int line = self->var_browser_->value();
	if( line <= 0 ) return;
	const char *name = self->var_browser_->text( line );
	in_variable_selected( (char *)name );
}

void MainWindow::populateVarList()
{
	var_browser_->clear();
	for( NCVar *v = variables; v != nullptr; v = (NCVar *)v->next )
		var_browser_->add( v->name );
}

void MainWindow::setLabel( int label_id, const char *s )
{
	if( label_id < 0 || label_id >= (int)(sizeof(labels_)/sizeof(labels_[0])) ) return;
	if( labels_[label_id] == nullptr ) return;
	labels_[label_id]->copy_label( s );
}

void MainWindow::setSensitive( int button_id, int state )
{
	if( button_id < 0 || button_id >= (int)(sizeof(buttons_)/sizeof(buttons_[0])) ) return;
	if( buttons_[button_id] == nullptr ) return;
	if( state ) buttons_[button_id]->activate();
	else buttons_[button_id]->deactivate();
}

void MainWindow::indicateActiveVar( const char *var_name )
{
	for( int i = 1; i <= var_browser_->size(); i++ ) {
		const char *t = var_browser_->text( i );
		if( t && std::strcmp( t, var_name ) == 0 ) {
			var_browser_->select( i );
			var_browser_->middleline( i );
			break;
		}
	}
}

void MainWindow::rebuildDimRow( DimRow &row )
{
	auto *group = new Fl_Pack( 0, 0, dim_pack_->w(), 24 );
	group->type( Fl_Pack::HORIZONTAL );
	group->spacing( 4 );
	row.name_box = new Fl_Box( 0, 0, 120, 22, "" );
	row.name_box->copy_label( row.name.c_str() );
	row.name_box->box( FL_FLAT_BOX );
	row.prev_btn = new Fl_Button( 0, 0, 24, 22, "@<" );
	row.value_box = new Fl_Box( 0, 0, 220, 22, "" );
	row.value_box->box( FL_DOWN_BOX );
	row.next_btn = new Fl_Button( 0, 0, 24, 22, "@>" );
	group->end();
	dim_pack_->add( group );

	// Callback data (dim name + modifier) must outlive the callback;
	// heap-allocated and intentionally never freed -- rows are rebuilt
	// only when the scan dimensions change, a rare, low-cardinality
	// event, so this is a small, bounded leak rather than a real one.
	row.prev_btn->callback( &MainWindow::dimStepCallback,
		new std::pair<std::string,int>( row.name, MOD_3 ) );
	row.next_btn->callback( &MainWindow::dimStepCallback,
		new std::pair<std::string,int>( row.name, MOD_1 ) );
}

void MainWindow::dimStepCallback( Fl_Widget *, void *data )
{
	auto *p = static_cast<std::pair<std::string,int>*>(data);
	view_change_cur_dim( (char *)p->first.c_str(), p->second );
}

void MainWindow::makeDimButtons( Stringlist *dim_list )
{
	clearDimButtons();
	for( Stringlist *s = dim_list; s != nullptr; s = (Stringlist *)s->next ) {
		DimRow row;
		row.name = s->string;
		rebuildDimRow( row );
		dim_rows_.push_back( row );
	}
	dim_pack_->redraw();
}

void MainWindow::clearDimButtons()
{
	dim_pack_->clear();
	dim_rows_.clear();
}

void MainWindow::fillDimInfo( NCDim *d, int /*please_flip*/ )
{
	if( d == nullptr ) return;
	for( auto &row : dim_rows_ ) {
		if( row.name == d->name ) {
			row.name_box->copy_label( d->long_name && d->long_name[0] ? d->long_name : d->name );
			break;
		}
	}
}

void MainWindow::setCurDimValue( const char *name, const char *value )
{
	for( auto &row : dim_rows_ ) {
		if( row.name == name ) {
			row.value_box->copy_label( value );
			return;
		}
	}
}

void MainWindow::indicateActiveDim( int /*dimension*/, const char *dim_name )
{
	for( auto &row : dim_rows_ )
		row.name_box->labelfont( row.name == dim_name ? FL_HELVETICA_BOLD : FL_HELVETICA );
	dim_pack_->redraw();
}

void MainWindow::draw2DField( const unsigned char *data, size_t width, size_t height, size_t /*timestep*/ )
{
	image_->setData( data, width, height );
}

void MainWindow::createColormap( const char *name, const unsigned char *r, const unsigned char *g, const unsigned char *b )
{
	NamedColormap cm;
	cm.name = name;
	std::memcpy( cm.r, r, 256 );
	std::memcpy( cm.g, g, 256 );
	std::memcpy( cm.b, b, 256 );
	colormaps_.push_back( cm );
	if( current_colormap_ < 0 ) {
		current_colormap_ = 0;
		image_->setColormap( r, g, b );
		colorbar_->setColormap( r, g, b );
	}
}

bool MainWindow::seenColormapName( const char *name ) const
{
	for( const auto &cm : colormaps_ )
		if( cm.name == name ) return true;
	return false;
}

void MainWindow::checkLegalColormapLoaded()
{
	if( current_colormap_ < 0 && !colormaps_.empty() ) {
		current_colormap_ = 0;
		image_->setColormap( colormaps_[0].r, colormaps_[0].g, colormaps_[0].b );
		colorbar_->setColormap( colormaps_[0].r, colormaps_[0].g, colormaps_[0].b );
	}
}

char *MainWindow::installNextColormap( int do_widgets )
{
	if( colormaps_.empty() ) return nullptr;
	current_colormap_ = (current_colormap_ + 1) % (int)colormaps_.size();
	auto &cm = colormaps_[current_colormap_];
	image_->setColormap( cm.r, cm.g, cm.b );
	colorbar_->setColormap( cm.r, cm.g, cm.b );
	if( do_widgets ) setLabel( LABEL_COLORMAP_NAME, cm.name.c_str() );
	return (char *)cm.name.c_str();
}

char *MainWindow::installPrevColormap( int do_widgets )
{
	if( colormaps_.empty() ) return nullptr;
	current_colormap_ = (current_colormap_ - 1 + (int)colormaps_.size()) % (int)colormaps_.size();
	auto &cm = colormaps_[current_colormap_];
	image_->setColormap( cm.r, cm.g, cm.b );
	colorbar_->setColormap( cm.r, cm.g, cm.b );
	if( do_widgets ) setLabel( LABEL_COLORMAP_NAME, cm.name.c_str() );
	return (char *)cm.name.c_str();
}

void MainWindow::createColorbar( float user_min, float user_max, int transform )
{
	colorbar_->setRange( user_min, user_max, transform );
}

void MainWindow::drawColorbar()
{
	colorbar_->redraw();
}

int MainWindow::set2DSize( size_t width, size_t height )
{
	static size_t last_w = 0, last_h = 0;
	if( width == last_w && height == last_h ) return 0;
	int retval = (width > last_w) ? 1 : -1;
	last_w = width;
	last_h = height;
	return retval;
}

void MainWindow::setCursorBusy( bool busy )
{
	if( busy ) win_->cursor( FL_CURSOR_WAIT );
	else win_->cursor( FL_CURSOR_DEFAULT );
}

void MainWindow::pixelToRgb( ncv_pixel pix, int *r, int *g, int *b ) const
{
	// Upstream's x_interface.c implementation returned X11 XColor-style
	// 16-bit channel values (do_print.c's only caller right-shifts by 8 to
	// get back to 8 bits: "fprintf(outf, "%02x%02x%02x", (r>>8), (g>>8),
	// (b>>8))"). Our colormap tables are plain 8-bit, so scale up the same
	// way X11 itself does (value16 = value8*257, i.e. value8 replicated
	// into both bytes) rather than changing do_print.cc's contract.
	unsigned char r8, g8, b8;
	if( current_colormap_ < 0 || current_colormap_ >= (int)colormaps_.size() ) {
		r8 = g8 = b8 = (unsigned char)pix;
	} else {
		const auto &cm = colormaps_[current_colormap_];
		r8 = cm.r[pix]; g8 = cm.g[pix]; b8 = cm.b[pix];
	}
	if( r ) *r = r8 * 257;
	if( g ) *g = g8 * 257;
	if( b ) *b = b8 * 257;
}

/* ===================== M4 dialogs ===================== */
/* Small modal dialogs, run with their own Fl::wait() loop (the standard
 * FLTK pattern for a blocking modal window: show(), set_modal(), spin until
 * it's hidden by a button callback). Replaces upstream's Xt dialog/range.c
 * /set_options.c-family widgets one dialog at a time; see PORTING.md. */

namespace {
struct ModalResult { bool ok = false; };

void modalOkCallback( Fl_Widget *w, void *data )
{
	static_cast<ModalResult*>(data)->ok = true;
	w->window()->hide();
}

void modalCancelCallback( Fl_Widget *w, void * )
{
	w->window()->hide();
}
} // namespace

void MainWindow::setOptionsDialog()
{
	Fl_Window win( 320, 190, "Options" );
	Fl_Check_Button autoscale( 10, 10, 300, 25, "Autoscale each frame" );
	autoscale.value( options.autoscale );
	Fl_Check_Button extra_info( 10, 40, 300, 25, "Show extra info" );
	extra_info.value( options.want_extra_info );
	Fl_Check_Button save_frames( 10, 70, 300, 25, "Save frames in memory" );
	save_frames.value( options.save_frames );
	Fl_Check_Button auto_overlay( 10, 100, 300, 25, "Automatic coastline overlay" );
	auto_overlay.value( options.auto_overlay );

	ModalResult result;
	Fl_Return_Button ok( 90, 145, 70, 30, "OK" );
	ok.callback( modalOkCallback, &result );
	Fl_Button cancel( 170, 145, 70, 30, "Cancel" );
	cancel.callback( modalCancelCallback, nullptr );

	win.end();
	win.set_modal();
	win.show();
	while( win.shown() ) Fl::wait();

	if( result.ok ) {
		options.autoscale = autoscale.value();
		options.want_extra_info = extra_info.value();
		options.save_frames = save_frames.value();
		options.auto_overlay = auto_overlay.value();
		view_draw( TRUE, FALSE );
	}
}

int MainWindow::rangeDialog( float old_min, float old_max, float global_min, float global_max,
		float *new_min, float *new_max, int *allvars )
{
	Fl_Window win( 320, 190, "Set Range" );
	char buf[64];

	Fl_Box global_box( 10, 10, 300, 20 );
	snprintf( buf, sizeof(buf), "Global range: %g to %g", global_min, global_max );
	global_box.copy_label( buf );

	Fl_Box min_label( 10, 40, 60, 25, "Min:" );
	Fl_Float_Input min_input( 80, 40, 220, 25 );
	snprintf( buf, sizeof(buf), "%g", old_min );
	min_input.value( buf );

	Fl_Box max_label( 10, 70, 60, 25, "Max:" );
	Fl_Float_Input max_input( 80, 70, 220, 25 );
	snprintf( buf, sizeof(buf), "%g", old_max );
	max_input.value( buf );

	Fl_Check_Button all_vars_cb( 10, 100, 300, 25, "Apply to all variables" );
	all_vars_cb.value( 0 );

	(void)min_label; (void)max_label;

	ModalResult result;
	Fl_Return_Button ok( 90, 145, 70, 30, "OK" );
	ok.callback( modalOkCallback, &result );
	Fl_Button cancel( 170, 145, 70, 30, "Cancel" );
	cancel.callback( modalCancelCallback, nullptr );

	win.end();
	win.set_modal();
	win.show();
	while( win.shown() ) Fl::wait();

	if( !result.ok ) return MESSAGE_CANCEL;

	*new_min = (float)atof( min_input.value() );
	*new_max = (float)atof( max_input.value() );
	if( allvars ) *allvars = all_vars_cb.value();
	return MESSAGE_OK;
}

int MainWindow::scanDimsDialog( Stringlist *dim_list, char *x_axis_name, char *y_axis_name,
		Stringlist **new_dim_list )
{
	std::vector<std::string> names;
	for( Stringlist *s = dim_list; s != nullptr; s = (Stringlist *)s->next )
		names.push_back( s->string );
	if( names.empty() ) return 0;

	Fl_Window win( 320, 150, "Set Scan Dimensions" );
	Fl_Box x_label( 10, 15, 60, 25, "X axis:" );
	Fl_Choice x_choice( 90, 15, 210, 25 );
	Fl_Box y_label( 10, 50, 60, 25, "Y axis:" );
	Fl_Choice y_choice( 90, 50, 210, 25 );
	(void)x_label; (void)y_label;

	int x_default = 0, y_default = 0;
	for( size_t i = 0; i < names.size(); i++ ) {
		x_choice.add( names[i].c_str() );
		y_choice.add( names[i].c_str() );
		if( x_axis_name && names[i] == x_axis_name ) x_default = (int)i;
		if( y_axis_name && names[i] == y_axis_name ) y_default = (int)i;
	}
	x_choice.value( x_default );
	y_choice.value( names.size() > 1 ? (int)((y_default == x_default) ? (x_default+1)%names.size() : y_default) : 0 );

	ModalResult result;
	Fl_Return_Button ok( 90, 105, 70, 30, "OK" );
	ok.callback( modalOkCallback, &result );
	Fl_Button cancel( 170, 105, 70, 30, "Cancel" );
	cancel.callback( modalCancelCallback, nullptr );

	win.end();
	win.set_modal();
	win.show();
	while( win.shown() ) Fl::wait();

	if( !result.ok || new_dim_list == nullptr ) return 0;

	// Build the returned list Y-axis first, then X-axis (matching upstream's
	// in_set_scan_dims contract: "first the name of the Y dimension, then
	// the name of the X dimension").
	*new_dim_list = nullptr;
	stringlist_add_string( new_dim_list, (char *)names[y_choice.value()].c_str(), nullptr, SLTYPE_NULL );
	stringlist_add_string( new_dim_list, (char *)names[x_choice.value()].c_str(), nullptr, SLTYPE_NULL );
	return 1;
}

int MainWindow::printerOptionsDialog( PrintOptions *po )
{
	Fl_Window win( 420, 300, "Printer Options" );
	char buf[64];

	Fl_Box dev_label( 10, 10, 60, 25, "Device:" );
	Fl_Round_Button dev_printer( 80, 10, 90, 25, "Printer" );
	Fl_Round_Button dev_file( 175, 10, 70, 25, "File" );
	dev_printer.type( FL_RADIO_BUTTON );
	dev_file.type( FL_RADIO_BUTTON );
	(po->output_device == DEVICE_PRINTER ? dev_printer : dev_file).setonly();
	Fl_Input outfile_input( 250, 10, 160, 25 );
	outfile_input.value( po->out_file_name );

	Fl_Box margin_label( 10, 45, 90, 25, "Margins (in):" );
	Fl_Box xmar_label( 100, 45, 20, 25, "X" );
	Fl_Float_Input xmar_input( 120, 45, 50, 25 );
	snprintf( buf, sizeof(buf), "%g", po->page_x_margin ); xmar_input.value( buf );
	Fl_Box ytmar_label( 180, 45, 60, 25, "Y top" );
	Fl_Float_Input ytmar_input( 240, 45, 50, 25 );
	snprintf( buf, sizeof(buf), "%g", po->page_upper_y_margin ); ytmar_input.value( buf );
	Fl_Box ybmar_label( 300, 45, 60, 25, "Y bot" );
	Fl_Float_Input ybmar_input( 360, 45, 50, 25 );
	snprintf( buf, sizeof(buf), "%g", po->page_lower_y_margin ); ybmar_input.value( buf );

	Fl_Box font_label( 10, 80, 90, 25, "Font:" );
	Fl_Input font_name_input( 100, 80, 120, 25 );
	font_name_input.value( po->font_name );
	Fl_Box fontsize_label( 230, 80, 40, 25, "Size" );
	Fl_Float_Input fontsize_input( 270, 80, 40, 25 );
	snprintf( buf, sizeof(buf), "%d", po->font_size ); fontsize_input.value( buf );
	Fl_Box headsize_label( 315, 80, 45, 25, "Head" );
	Fl_Float_Input headsize_input( 360, 80, 40, 25 );
	snprintf( buf, sizeof(buf), "%d", po->header_font_size ); headsize_input.value( buf );

	Fl_Check_Button include_title( 10, 115, 190, 25, "Title" );
	include_title.value( po->include_title );
	Fl_Check_Button include_axis( 10, 140, 190, 25, "Axis labels" );
	include_axis.value( po->include_axis_labels );
	Fl_Check_Button include_extra( 10, 165, 190, 25, "Extra info" );
	include_extra.value( po->include_extra_info );
	Fl_Check_Button include_outline( 210, 115, 190, 25, "Outline" );
	include_outline.value( po->include_outline );
	Fl_Check_Button include_id( 210, 140, 190, 25, "ID" );
	include_id.value( po->include_id );
	Fl_Check_Button test_only( 210, 165, 190, 25, "No image (test only)" );
	test_only.value( po->test_only );

	ModalResult result;
	Fl_Return_Button ok( 190, 250, 70, 30, "OK" );
	ok.callback( modalOkCallback, &result );
	Fl_Button cancel( 270, 250, 70, 30, "Cancel" );
	cancel.callback( modalCancelCallback, nullptr );

	win.end();
	win.set_modal();
	win.show();
	while( win.shown() ) Fl::wait();

	if( !result.ok ) return MESSAGE_CANCEL;

	po->output_device = dev_printer.value() ? DEVICE_PRINTER : DEVICE_FILE;
	strncpy( po->out_file_name, outfile_input.value(), sizeof(po->out_file_name)-1 );
	po->page_x_margin = (float)atof( xmar_input.value() );
	po->page_upper_y_margin = (float)atof( ytmar_input.value() );
	po->page_lower_y_margin = (float)atof( ybmar_input.value() );
	strncpy( po->font_name, font_name_input.value(), sizeof(po->font_name)-1 );
	po->font_size = atoi( fontsize_input.value() );
	po->header_font_size = atoi( headsize_input.value() );
	po->include_title = include_title.value();
	po->include_axis_labels = include_axis.value();
	po->include_extra_info = include_extra.value();
	po->include_outline = include_outline.value();
	po->include_id = include_id.value();
	po->test_only = test_only.value();

	return MESSAGE_OK;
}

} // namespace ncview_ui

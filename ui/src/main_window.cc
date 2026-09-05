#include "ncview_ui/main_window.h"

#include <cstdio>
#include <cstring>

#include <FL/Fl.H>
#include <FL/fl_draw.H>

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
			return 1;
		}
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

} // namespace ncview_ui

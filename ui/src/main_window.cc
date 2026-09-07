#include "ncview_ui/main_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <typeinfo>

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/names.h>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Float_Input.H>
#include <FL/Fl_Hor_Slider.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multi_Label.H>
#include <FL/Fl_Native_File_Chooser.H>
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

namespace {
// Small horizontal-gradient swatch for a colormap combo entry -- one column
// per on-screen pixel, sampled across the full [0,255] table so the preview
// looks like a miniature colorbar rather than a solid block.
constexpr int kColormapPreviewW = 32, kColormapPreviewH = 14;

Fl_RGB_Image *buildColormapPreview( const unsigned char *r, const unsigned char *g, const unsigned char *b )
{
	// alloc_array=1 below hands ownership of this buffer to the Fl_RGB_Image,
	// which delete[]s it when the image itself is destroyed.
	auto *buf = new unsigned char[ kColormapPreviewW * kColormapPreviewH * 3 ];
	for( int x = 0; x < kColormapPreviewW; x++ ) {
		int idx = x * 255 / (kColormapPreviewW - 1);
		for( int y = 0; y < kColormapPreviewH; y++ ) {
			unsigned char *px = buf + (y*kColormapPreviewW + x) * 3;
			px[0] = r[idx]; px[1] = g[idx]; px[2] = b[idx];
		}
	}
	auto *img = new Fl_RGB_Image( buf, kColormapPreviewW, kColormapPreviewH, 3 );
	img->alloc_array = 1;
	return img;
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
	// A size change means a different variable or scan-dim layout is now
	// showing; start that view fresh rather than carrying over a zoom/pan
	// that was framed for the old data.
	if( width != width_ || height != height_ ) {
		zoom_ = 1.0;
		panx_ = pany_ = 0.0;
	}
	width_ = width;
	height_ = height;
	pixels_.assign( data, data + width*height );
	rgb_buf_.resize( width*height*3 );
	for( size_t i = 0; i < width*height; i++ )
		lookup( colormap_r_, colormap_g_, colormap_b_, pixels_[i], &rgb_buf_[i*3] );
	redraw();
}

namespace {
struct ZoomDrawCtx {
	const unsigned char *rgb;
	size_t width, height;
	double zoom, ox, oy;             // ox,oy: window-relative origin of buffer (0,0)
	unsigned char bg_r, bg_g, bg_b;  // shown outside the image bounds
};
} // namespace

void ImageView::draw()
{
	if( rgb_buf_.empty() || width_ == 0 || height_ == 0 ) {
		fl_color( FL_DARK2 );
		fl_rectf( x(), y(), w(), h() );
		return;
	}

	ZoomDrawCtx ctx;
	ctx.rgb = rgb_buf_.data();
	ctx.width = width_;
	ctx.height = height_;
	ctx.zoom = zoom_;
	ctx.ox = x() + (w() - width_*zoom_) / 2.0 + panx_;
	ctx.oy = y() + (h() - height_*zoom_) / 2.0 + pany_;
	Fl::get_color( FL_DARK2, ctx.bg_r, ctx.bg_g, ctx.bg_b );

	// Draw via a per-scanline callback rather than fl_draw_image() on a
	// pre-scaled buffer: this lets the display zoom continuously (including
	// shrinking to fit and magnifying well past the data's native
	// resolution) via simple nearest-neighbor sampling, without ever
	// resampling/copying the underlying rgb_buf_ itself.
	fl_draw_image( []( void *data, int cx, int cy, int w_line, unsigned char *buf ) {
			auto *c = static_cast<ZoomDrawCtx*>( data );
			double by = ( cy - c->oy ) / c->zoom;
			bool row_in_range = by >= 0.0 && by < (double)c->height;
			size_t row = row_in_range ? (size_t)by : 0;
			for( int i = 0; i < w_line; i++ ) {
				double bx = ( (cx + i) - c->ox ) / c->zoom;
				if( row_in_range && bx >= 0.0 && bx < (double)c->width ) {
					size_t idx = ( row * c->width + (size_t)bx ) * 3;
					buf[i*3+0] = c->rgb[idx+0];
					buf[i*3+1] = c->rgb[idx+1];
					buf[i*3+2] = c->rgb[idx+2];
				} else {
					buf[i*3+0] = c->bg_r;
					buf[i*3+1] = c->bg_g;
					buf[i*3+2] = c->bg_b;
				}
			}
		}, &ctx, x(), y(), w(), h(), 3 );
}

void ImageView::screenToBuffer( int win_x, int win_y, int *bx, int *by ) const
{
	double ox = x() + (w() - width_*zoom_) / 2.0 + panx_;
	double oy = y() + (h() - height_*zoom_) / 2.0 + pany_;
	if( bx ) *bx = (int)std::floor( (win_x - ox) / zoom_ );
	if( by ) *by = (int)std::floor( (win_y - oy) / zoom_ );
}

void ImageView::zoomAt( int win_x, int win_y, double factor )
{
	if( width_ == 0 || height_ == 0 ) return;
	double new_zoom = zoom_ * factor;
	if( new_zoom < kMinZoom ) new_zoom = kMinZoom;
	if( new_zoom > kMaxZoom ) new_zoom = kMaxZoom;
	if( new_zoom == zoom_ ) return;

	// Keep the buffer point currently under the cursor fixed on screen
	// (the usual "zoom toward the pointer" behavior), rather than zooming
	// around the image center.
	int bx, by;
	screenToBuffer( win_x, win_y, &bx, &by );

	zoom_ = new_zoom;
	double base_ox = x() + (w() - width_*zoom_) / 2.0;
	double base_oy = y() + (h() - height_*zoom_) / 2.0;
	panx_ = win_x - base_ox - bx*zoom_;
	pany_ = win_y - base_oy - by*zoom_;

	redraw();
}

int ImageView::handle( int event )
{
	switch( event ) {
		// FL_ENTER must be accepted (return 1) here, not just FL_MOVE:
		// Fl_Group::handle()'s FL_ENTER/FL_MOVE case sends FL_ENTER (not
		// FL_MOVE) the moment the mouse first enters a child and only
		// latches Fl::belowmouse() onto that child if its handle() returns
		// non-zero for that FL_ENTER. Since this case list previously
		// didn't include FL_ENTER, it fell through to Fl_Widget::handle()
		// (returns 0), so Fl_Group never latched belowmouse onto this
		// widget -- every subsequent plain mouse move (no button held) re-
		// entered the same "first entry" branch and got resent as another
		// FL_ENTER instead of FL_MOVE, so the position/value readout never
		// updated on hover. It worked only during an active drag because
		// FL_PUSH/FL_DRAG/FL_RELEASE route via Fl::pushed(), a completely
		// separate mechanism from belowmouse-based FL_MOVE dispatch.
		case FL_ENTER:
		case FL_PUSH:
		case FL_DRAG:
		case FL_MOVE: {
			if( event == FL_PUSH ) {
				press_x_ = Fl::event_x();
				press_y_ = Fl::event_y();
				pan_start_x_ = panx_;
				pan_start_y_ = pany_;
				dragging_ = false;
			} else if( event == FL_DRAG && Fl::event_button1() ) {
				// Left-button drag pans the view -- replaces upstream's
				// discrete Blowup button with direct, continuous navigation.
				// Gated by a small movement threshold so it doesn't eat the
				// plain-click-to-plot / Ctrl-click-to-set-min/max gestures
				// handled below on FL_RELEASE.
				int dx = Fl::event_x() - press_x_;
				int dy = Fl::event_y() - press_y_;
				if( !dragging_ && ( std::abs( dx ) > 3 || std::abs( dy ) > 3 ) )
					dragging_ = true;
				if( dragging_ ) {
					panx_ = pan_start_x_ + dx;
					pany_ = pan_start_y_ + dy;
					redraw();
				}
			}
			unsigned int mask = 0;
			if( Fl::event_button1() ) mask |= 1;
			if( Fl::event_button2() ) mask |= 2;
			if( Fl::event_button3() ) mask |= 4;
			int bx, by;
			screenToBuffer( Fl::event_x(), Fl::event_y(), &bx, &by );
			view_report_position( bx, by, mask );
			// Middle-button press/drag highlights the corresponding cell in
			// the data-edit window, if one is open (matches upstream's
			// Btn2Up/Btn2Motion -> do_set_dataedit_place() translation).
			if( (event == FL_PUSH || event == FL_DRAG) && Fl::event_button2() )
				set_dataedit_place();
			return 1;
		}
		case FL_MOUSEWHEEL: {
			// Scroll to zoom, centered on the cursor -- replaces upstream's
			// discrete Blowup/Bl.Type buttons with continuous zoom.
			double factor = std::pow( 1.1, -Fl::event_dy() );
			zoomAt( Fl::event_x(), Fl::event_y(), factor );
			return 1;
		}
		case FL_RELEASE:
			// Matches upstream's ccontour_widget translations: a plain
			// left-button release pops up an XY plot along the cursor's
			// position; the same with Ctrl held instead sets the current
			// min/max (Btn1 -> min, Btn3 -> max) from the value under the
			// cursor. Suppressed if this release ends a pan drag rather
			// than an actual click.
			if( Fl::event_button() == FL_LEFT_MOUSE ) {
				if( dragging_ ) dragging_ = false;
				else if( Fl::event_state( FL_CTRL ) ) set_min_from_curdata();
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

namespace {
// Direct port of upstream cbar.c's "nice round numbers" tick-level picker
// (mynormalize/nlev_from_step/genlevs), unchanged, so the colorbar lands on
// the same 1/2/5 x10^n step sizes and tick count upstream's does instead of
// just labeling the two endpoints.
void cbarNormalize( double value, double *mantissa, double *exponent )
{
	if( value == 0.0 ) { *mantissa = 0.0; *exponent = 0.0; return; }
	double q = std::log10( value );
	*exponent = (double)(int)q;
	*mantissa = value / std::pow( 10.0, *exponent );
	if( q < 0.0 ) { *exponent -= 1.0; *mantissa *= 10.0; }
}

void cbarNlevFromStep( double step, double mindat, double maxdat, int *nlev, double *start )
{
	int n0 = (int)(maxdat/step);
	double cursor = (double)n0 * step;
	while( cursor > mindat ) { n0--; cursor = (double)n0 * step; }
	int n1 = (int)(mindat/step);
	cursor = (double)n1 * step;
	while( cursor < maxdat ) { n1++; cursor = (double)n1 * step; }
	*nlev = n1 - n0 + 1;
	*start = (double)n0 * step;
}

bool cbarGenlevs( double mindat, double maxdat, int nlevels, double *start, int *nlevs, double *step )
{
	if( nlevels < 2 || maxdat <= mindat ) return false;
	static const int kTrial[4] = { 1, 2, 5, 10 };
	double trialstep = (maxdat - mindat) / (double)(nlevels - 1);
	double mant, expon;
	cbarNormalize( trialstep, &mant, &expon );
	double fact = std::pow( 10.0, expon );
	for( int i = 0; i < 3; i++ ) {
		if( mant < kTrial[i] || mant > kTrial[i+1] ) continue;
		double step1 = kTrial[i]*fact, step2 = kTrial[i+1]*fact;
		int n1, n2;
		double start1, start2;
		cbarNlevFromStep( step1, mindat, maxdat, &n1, &start1 );
		cbarNlevFromStep( step2, mindat, maxdat, &n2, &start2 );
		if( std::abs( n1 - nlevels ) <= std::abs( n2 - nlevels ) ) {
			*step = step1; *nlevs = n1; *start = start1;
		} else {
			*step = step2; *nlevs = n2; *start = start2;
		}
		return true;
	}
	return false;
}
} // namespace

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

void Colorbar::setRange( float user_min, float user_max, Transform transform )
{
	user_min_ = user_min;
	user_max_ = user_max;
	transform_ = transform;
	redraw();
}

void Colorbar::draw()
{
	int n_colors = options.n_colors > 0 ? options.n_colors : 200;
	int width = w() > 0 ? w() : 1;
	for( int px = 0; px < w(); px++ ) {
		// Mirrors util.cc:data_to_pixels' pixel-index formula exactly
		// (transform, then invert_colors, then scale by n_colors) --
		// upstream's cbar.c does the same in cbar_make(). Without this the
		// colorbar shows a plain linear gradient that no longer matches
		// the image whenever a transform or "Invert Colormap" is active.
		double normval = (double)px / (double)width;
		switch( transform_ ) {
			case Transform::Hi:     normval = normval*normval*normval*normval; break;
			case Transform::Low:    normval = sqrt( sqrt( normval ) ); break;
			case Transform::Center: normval = atan( (normval-0.5)*8.0 )/3.1415926536 + 0.5; break;
			default: break;
		}
		if( options.invert_colors ) normval = 1.0 - normval;
		int idx = 10 + (int)(normval * n_colors);
		if( idx < 0 ) idx = 0;
		if( idx > 255 ) idx = 255;
		fl_color( fl_rgb_color( colormap_r_[idx], colormap_g_[idx], colormap_b_[idx] ) );
		fl_line( x()+px, y(), x()+px, y()+h() );
	}
	if( user_max_ <= user_min_ ) return;

	// Upstream targets one label per ~48px (6 chars * 8px, cbar_make()'s
	// "typical_label_width") and picks a "nice" 1/2/5x10^n step to hit that
	// count, rather than just labeling the two endpoints.
	int nlev_target = w() / 48 - 2;
	if( nlev_target < 2 ) return;

	double start, step;
	int nlev;
	if( !cbarGenlevs( user_min_, user_max_, nlev_target, &start, &nlev, &step ) )
		return;

	fl_color( FL_BLACK );
	fl_font( FL_HELVETICA, 10 );
	double drange = user_max_ - user_min_;
	for( int i = 0; i < nlev; i++ ) {
		double val = start + step*i;
		double xfrac = (val - user_min_) / drange;
		if( xfrac < 0.0 || xfrac > 1.0 ) continue;

		char buf[64];
		snprintf( buf, sizeof(buf), "%g", val );
		int ptx = x() + (int)(xfrac * w() + 0.5);
		int sw = (int)fl_width( buf );
		int ptx_text = ptx - sw/2;
		if( ptx_text < x() ) ptx_text = x();
		if( ptx_text + sw > x()+w() ) ptx_text = x()+w() - sw;

		fl_line( ptx, y()+h(), ptx, y()+h()+3 );
		fl_draw( buf, ptx_text, y()+h()+13 );
	}
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
	win_ = new NcviewWindow( W, H, "ncview" );

	// Fl_Sys_Menu_Bar: on macOS this becomes the native system menu bar at
	// the top of the screen (a documented drop-in replacement for
	// Fl_Menu_Bar -- its own constructor detaches it from win_ once built,
	// see Fl_Sys_Menu_Bar.cxx); on every other platform it behaves exactly
	// like an ordinary in-window Fl_Menu_Bar.
	menu_bar_ = new Fl_Sys_Menu_Bar( 0, 0, W, kMenuBarH );
	// Every action below just forwards to the exact same in_button_pressed()
	// path a toolbar button's click already used (buttonCallback() is a
	// plain Fl_Callback -- Fl_Menu_Item::callback is the same typedef, so
	// it works unchanged as a menu item's callback too), grouped as: File
	// (whole-session actions), Edit (dialogs that change/configure state),
	// View (display toggles and passive info). These are the 11 buttons
	// upstream and earlier revisions of this port kept on the main toolbar
	// that are used far less often than the animation transport and Min/
	// Max, which stay as buttons below.
	auto add_menu_item = [this]( const char *path, Button id ) {
		int idx = menu_bar_->add( path, 0, &MainWindow::buttonCallback, (void*)(intptr_t)static_cast<int>(id) );
		menu_items_[static_cast<int>(id)] = const_cast<Fl_Menu_Item*>( menu_bar_->menu() + idx );
	};
	add_menu_item( "File/Print...",              Button::Print );
	add_menu_item( "File/Quit",                  Button::Quit );
	add_menu_item( "Edit/Edit Data...",          Button::Edit );
	add_menu_item( "Edit/Set Scan Dimensions...", Button::Dimset );
	add_menu_item( "Edit/Options...",            Button::Options );
	add_menu_item( "View/Info",                  Button::Info );
	add_menu_item( "View/Range...",              Button::Range );
	add_menu_item( "View/Transform",             Button::Transform );
	add_menu_item( "View/Interp",                Button::BlowupType );
	add_menu_item( "View/Invert Physical",       Button::InvertPhysical );
	add_menu_item( "View/Invert Colormap",       Button::InvertColormap );

	// Info rows above the image. Packing unrelated fields side-by-side in
	// fixed-width columns (the previous layout, and upstream's before it)
	// looks fine only when every field happens to be near its column's
	// worst-case width -- with typical short content (a two-letter variable
	// name, "Linear", an empty optional field) it instead reads as a
	// scattered mix of isolated words with big dead gaps around them. So
	// each independent, potentially-long field (name, frame/date, range,
	// position, per-variable extra info, scalar coords, extra time info)
	// gets its own full-width row and stacks vertically -- consistent left
	// margin, no visual "randomness" regardless of how much of any one row
	// is actually filled in. The one exception is the colormap/transform/
	// interpolation trio: always present, always short (a colormap name, one
	// of 4 transform words, "Repl"/"Bi-lin"), so they're packed tightly on
	// one row instead of each claiming a full line for a couple of words.
	// FL_ALIGN_CLIP is a belt-and-suspenders backstop: if some future
	// content still ends up wider than its column, it gets clipped instead
	// of silently drawing over whatever comes after it.
	// Each row above the colormap/transform/interpolation trio gets its own
	// bordered background box too -- same FL_ENGRAVED_BOX treatment as that
	// trio's box below, for a consistent look, and each created (so drawn)
	// before the label(s) that sit on top of it. These rows sit directly
	// adjacent to each other (each one's y is exactly the previous one's
	// y+h, no gap), so unlike a normal "pad outward a bit" border inset,
	// insetting *inward* (y+1, h-2) is what actually keeps consecutive
	// boxes from overlapping -- padding outward here would make each box 6px
	// taller than its 18px row spacing, overlapping the next row's box by
	// that much (this is exactly what the first version of this got wrong).
	// Width is fixed here and stretched to the window's right edge in
	// layout(), same as the labels themselves.
	info_row_boxes_[0] = new Fl_Box( 6, kMenuBarH+6, W-12, 18 );
	labels_[static_cast<int>(Label::Title)]        = new Fl_Box( 10, kMenuBarH+5, W-20, 20 );
	info_row_boxes_[1] = new Fl_Box( 6, kMenuBarH+26, W-12, 16 );
	labels_[static_cast<int>(Label::ScanvarName)] = new Fl_Box( 10, kMenuBarH+25, W-20, 18 );
	info_row_boxes_[2] = new Fl_Box( 6, kMenuBarH+44, W-12, 16 );
	labels_[static_cast<int>(Label::ScanPlace)]   = new Fl_Box( 10, kMenuBarH+43, W-20, 18 );
	info_row_boxes_[3] = new Fl_Box( 6, kMenuBarH+62, W-12, 16 );
	labels_[static_cast<int>(Label::DataExtrema)] = new Fl_Box( 10, kMenuBarH+61, 300, 18 );
	// Wide enough to actually show the full "Current: (i=.., j=..) val
	// (x=.., y=..)" string view_report_position() builds -- the old fixed
	// 200px width clipped it right after "(x=", silently hiding the x/y
	// coordinate values even though they were always part of the label
	// text and updating on every mouse move. Resized to track the window
	// edge in layout() below, same as Label::Title.
	labels_[static_cast<int>(Label::DataValue)]   = new Fl_Box( 320, kMenuBarH+61, W-330, 18 );
	for( auto *b : info_row_boxes_ ) b->box( FL_ENGRAVED_BOX );
	// A bordered box behind the trio below ties them together visually as
	// one "display settings" unit, distinct from the free-form info lines
	// around it -- created (and so drawn) before the labels it sits behind.
	// Same inward inset as the row boxes above, for the same reason (this
	// row starts right where Label::DataExtrema/DataValue's row ends).
	auto *display_settings_box = new Fl_Box( 6, kMenuBarH+80, 280, 16 );
	display_settings_box->box( FL_ENGRAVED_BOX );
	// BlowupType ("Repl"/"Bi-lin") goes first/leftmost: unlike ColormapName
	// and Transform, it's essentially always meaningful, so it anchors to
	// the box's actual left edge instead of sitting stranded in the middle
	// whenever the other two happen to be blank.
	labels_[static_cast<int>(Label::BlowupType)]  = new Fl_Box( 10, kMenuBarH+79, 70, 18 );
	// No Label::Blowup ("M X<n>") box -- it showed core's discrete
	// pre-zoom pixel-buffer scale factor, which upstream's now-removed
	// Button::Blowup let you cycle. With that gone (replaced by ImageView's
	// continuous scroll/drag zoom, which this label never reflected anyway)
	// it was a static, unexplained number nobody could act on.
	labels_[static_cast<int>(Label::Transform)]    = new Fl_Box( 85, kMenuBarH+79, 70, 18 );
	labels_[static_cast<int>(Label::ColormapName)]= new Fl_Box( 160, kMenuBarH+79, 120, 18 );
	labels_[static_cast<int>(Label::CcInfo1)]     = new Fl_Box( 10, kMenuBarH+97, W-20, 18 );
	labels_[static_cast<int>(Label::Skip)]         = new Fl_Box( 10, kMenuBarH+115, 150, 18 );
	labels_[static_cast<int>(Label::ScalarDims)]  = new Fl_Box( 170, kMenuBarH+115, W-180, 18 );
	labels_[static_cast<int>(Label::CcInfo2)]     = new Fl_Box( 10, kMenuBarH+133, W-20, 18 );
	for( auto *b : labels_ ) if( b ) { b->box( FL_NO_BOX ); b->align( FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_CLIP ); }

	var_pack_ = new Fl_Pack( 10, 100, 180, H-220 );
	var_pack_->type( Fl_Pack::VERTICAL );
	var_pack_->spacing( 2 );
	var_pack_->end();

	image_ = new ImageView( 200, 100, W-210, H-320 );
	colorbar_ = new Colorbar( 200, H-210, W-210, 20 );

	dim_pack_ = new Fl_Group( 10, H-180, W-20, 100 );
	dim_pack_->end();

	button_bar_ = new Fl_Pack( 10, H-70, W-20, 60 );
	button_bar_->type( Fl_Pack::VERTICAL );  // a column of per-row horizontal packs; see rebuildButtonBar()
	button_bar_->spacing( 2 );

	win_->end();
	// No win_->resizable(...): layout() (below) repositions/resizes every
	// managed widget itself on every resize (via NcviewWindow::on_resize),
	// so FLTK's own generic proportional child-resize would just be
	// redundant work immediately overwritten by layout() -- leaving no
	// resizable() child makes Fl_Group::resize() a no-op for our children.
	win_->on_resize = [this]( int w, int h ) { layout( w, h ); };
	win_->size_range( 700, 500 );
	layout( W, H );
}

void MainWindow::layout( int w, int h )
{
	const int kSideMargin = 10;
	const int kImageX = 200;
	// menu_bar_'s own height, plus 8 info rows (5,25,43,61,79,97,115,133
	// within that, each 18px, 20 for the title) -- see the row layout
	// comment in the constructor -- plus a small gap before
	// var_pack_/image_/dim_pack_ start.
	const int kTopY = kMenuBarH + 152;
	const int kColorbarH = 20;
	const int kColorbarGap = 10;
	// Colorbar::draw() puts its tick labels ~13px below the swatch (plus
	// font ascent/descent), so the gap before dim_pack_ needs to clear that
	// text height -- 10px wasn't enough and let tick labels bleed into (and
	// render underneath) the dimension row widgets below.
	const int kColorbarLabelGap = 20;
	const int kDimPackH = 100;
	const int kDimGap = 10;
	const int kBottomMargin = 10;
	const int kVarPackGap = 40;  // matches the original fixed layout's var_pack_-to-dim_pack_ gap

	menu_bar_->resize( 0, 0, w, kMenuBarH );

	// Bottom-up: the button bar's height depends on how many rows the
	// current width wraps it into (rebuildButtonBar()), which then pushes
	// everything above it up or down.
	int button_bar_w = w - 2*kSideMargin;
	int button_bar_h = rebuildButtonBar( button_bar_w );
	int button_bar_y = h - kBottomMargin - button_bar_h;

	int dim_pack_y = button_bar_y - kDimGap - kDimPackH;
	int colorbar_y = dim_pack_y - kColorbarLabelGap - kColorbarH;
	int image_h = colorbar_y - kColorbarGap - kTopY;
	if( image_h < 40 ) image_h = 40;  // keep something sane at extreme window sizes

	int right_w = w - kImageX - kSideMargin;
	if( right_w < 40 ) right_w = 40;

	button_bar_->resize( kSideMargin, button_bar_y, button_bar_w, button_bar_h );
	// Same x/width as colorbar_/image_ (not the full window, which would
	// also span the var_pack_ variable-list column to its left) -- this is
	// what actually keeps the dimension rows aligned under the colorbar
	// they control, on every resize.
	dim_pack_->resize( kImageX, dim_pack_y, right_w, kDimPackH );
	colorbar_->resize( kImageX, colorbar_y, right_w, kColorbarH );
	image_->resize( kImageX, kTopY, right_w, image_h );

	// dim_pack_'s x/y/w just changed; recompute every row's own position
	// from that (recenterDimRow() reads dim_pack_ directly, not any row's
	// own possibly-stale position) and re-center its children within it.
	for( auto &row : dim_rows_ ) recenterDimRow( row );

	int var_pack_h = dim_pack_y - kVarPackGap - kTopY;
	if( var_pack_h < 40 ) var_pack_h = 40;
	var_pack_->resize( kSideMargin, kTopY, 180, var_pack_h );

	// These labels' text is unbounded in length (a variable name, a frame's
	// date string with bounds, a mouse-position readout, "extra info"), so
	// rather than pick a fixed width that's either too cramped on a small
	// window or wastes space on a wide one, stretch each to the window's
	// right edge on every resize -- same as Title above.
	auto stretch_to_edge = [&]( Label id ) {
		Fl_Box *b = labels_[static_cast<int>(id)];
		if( b == nullptr ) return;
		b->size( w - kSideMargin - b->x(), b->h() );
	};
	if( labels_[static_cast<int>(Label::Title)] ) labels_[static_cast<int>(Label::Title)]->size( w - 2*kSideMargin, labels_[static_cast<int>(Label::Title)]->h() );
	stretch_to_edge( Label::ScanvarName );
	stretch_to_edge( Label::ScanPlace );
	stretch_to_edge( Label::DataValue );
	stretch_to_edge( Label::CcInfo1 );
	stretch_to_edge( Label::ScalarDims );
	stretch_to_edge( Label::CcInfo2 );

	// The decorative row boxes behind Title/ScanvarName/ScanPlace/
	// DataExtrema+DataValue track the window's right edge the same way
	// (their x=6 inset, vs. the labels' x=10, is matched on the right too).
	for( auto *b : info_row_boxes_ )
		if( b ) b->size( w - 6 - b->x(), b->h() );

	win_->redraw();
}

// Explicit per-button pixel widths (rather than one fixed size for all)
// since a uniform 60px was too narrow for "Restart"/etc, leaving their
// labels crowding the button edges.
//
// Just the animation transport plus Min/Max -- the controls actually
// clicked often enough, mid-session, to earn a permanently visible,
// single-click button. Everything else (Inv.Phys, Inv.Cmap, Transform,
// Interp, DimSet, Range, Edit, Info, Print, Options, Quit) moved into
// menu_bar_ instead (see the constructor) -- occasional actions and
// settings that are fine behind one extra click, freeing this bar to fit
// on a single row instead of wrapping.
//
// No Button::ColormapSelect entry here -- replaced by the colormap combobox
// in var_pack_ (see rebuildColormapChoice()), which shows every colormap's
// name and a preview swatch instead of cycling through them blind one at a
// time. Button::ColormapSelect/do_colormap_sel() still exist for the
// NCVIEW_TEST_BUTTON headless-test hook and any script driving buttons by
// id directly; they just have no on-screen button anymore.
struct ButtonSpec { Button id; const char *text; int width; };
static const ButtonSpec kButtonSpecs[] = {
	{ Button::Rewind, "@|<", 40 }, { Button::Backwards, "@<", 40 }, { Button::Pause, "@||", 40 },
	{ Button::Forward, "@>", 40 }, { Button::Fastforward, "@>|", 40 }, { Button::Restart, "Restart", 65 },
	// No Button::Blowup here -- replaced by ImageView's scroll-to-zoom (mouse
	// wheel) and drag-to-pan (left-button drag), which give continuous
	// navigation instead of upstream's discrete button.
	{ Button::Minimum, "Min", 50 }, { Button::Maximum, "Max", 50 },
};

// Rebuilds the button bar as however many rows of buttons fit in
// available_width -- replaces the old single Fl_Pack::HORIZONTAL row, which
// simply ran off the right edge of the window once the buttons' total width
// (~1100px across 19 buttons) exceeded it (which it always did, even at the
// original fixed 900px window width). button_bar_ itself is now a VERTICAL
// pack of per-row HORIZONTAL packs, rebuilt every time available_width
// changes (window resize) so it always wraps instead of overflowing.
// Returns the total height needed for all the rows produced.
int MainWindow::rebuildButtonBar( int available_width )
{
	button_bar_->clear();
	if( available_width < 60 ) available_width = 60;

	const int kButtonHeight = 26, kSpacing = 2;
	Fl_Pack *row = nullptr;
	int row_width = 0;  // width used so far in the current row, incl. inter-button spacing
	int n_rows = 0;

	// Starts a new row whenever the current one has no room left for
	// item_width -- shared by the plain buttons below and the "Delay:"
	// label+slider after them, so both wrap the same way.
	auto ensure_row = [&]( int item_width ) {
		int with_this = row_width + ( row_width > 0 ? kSpacing : 0 ) + item_width;
		if( row == nullptr || with_this > available_width ) {
			row = new Fl_Pack( 0, 0, available_width, kButtonHeight );
			row->type( Fl_Pack::HORIZONTAL );
			row->spacing( kSpacing );
			// Every child below is added explicitly via row->add(), not
			// FLTK's construct-time auto-parenting, so this doesn't need to
			// stay "current" for that -- but leaving it current (Fl_Group's
			// constructor always calls current(this), and nothing else ever
			// closes it) meant Fl_Group::current() stayed pointed at the
			// last-built row long after rebuildButtonBar() returned. Any
			// later top-level Fl_Window built anywhere in the app (e.g. the
			// "Plot Along Dimension" popup, plot_window.cc's
			// PlotWindow::create) then got silently auto-parented as an X11
			// child of *this* row -- and transitively of the main window --
			// instead of becoming its own top-level window, which is exactly
			// the "plot window draws inside the main window" bug.
			row->end();
			button_bar_->add( row );
			row_width = 0;
			n_rows++;
		}
	};

	for( const auto &spec : kButtonSpecs ) {
		ensure_row( spec.width );
		auto *btn = new Fl_Button( 0, 0, spec.width, kButtonHeight, spec.text );
		btn->callback( &MainWindow::buttonCallback, (void*)(intptr_t)static_cast<int>(spec.id) );
		row->add( btn );
		buttons_[static_cast<int>(spec.id)] = btn;
		row_width += ( row_width > 0 ? kSpacing : 0 ) + spec.width;
	}

	// "Delay:" label + a slider controlling options.frame_delay (0.0 =
	// fastest, 1.0 = slowest -- see do_buttons.cc's DELAY_DELTA/
	// DELAY_OFFSET, which turn this into the actual timer interval for
	// Rewind/Fastforward's held-down auto-repeat). Upstream's
	// x_interface.c placed this scrollbar directly in the button box too
	// (scrollspeed_widget) rather than in a dialog, adjusted by feel while
	// an animation is actually playing -- so it stays here rather than
	// moving into menu_bar_ with the occasional-use actions.
	const int kDelayLabelW = 40, kDelaySliderW = 90;
	ensure_row( kDelayLabelW + kSpacing + kDelaySliderW );
	auto *delay_label = new Fl_Box( 0, 0, kDelayLabelW, kButtonHeight, "Delay:" );
	row->add( delay_label );
	row_width += ( row_width > 0 ? kSpacing : 0 ) + kDelayLabelW;
	auto *delay_slider = new Fl_Hor_Slider( 0, 0, kDelaySliderW, kButtonHeight );
	delay_slider->bounds( 0.0, 1.0 );
	delay_slider->value( options.frame_delay );
	delay_slider->callback( []( Fl_Widget *w, void * ) {
		options.frame_delay = static_cast<Fl_Slider*>(w)->value();
	} );
	row->add( delay_slider );
	row_width += kSpacing + kDelaySliderW;

	return n_rows*kButtonHeight + ( n_rows > 0 ? (n_rows-1)*kSpacing : 0 );
}

void MainWindow::buttonCallback( Fl_Widget *, void *data )
{
	Button id = static_cast<Button>( (int)(intptr_t)data );
	in_button_pressed( id, Modifier::M1 );
}

void MainWindow::varChoiceCallback( Fl_Widget *w, void * )
{
	// The variable name is stashed as each menu item's user_data (set in
	// populateVarList()), not read back from the item's label -- labels are
	// escaped ('/' and '&' are FLTK menu-path/shortcut metacharacters) so
	// they can't be used directly as the real NCVar name.
	auto *choice = static_cast<Fl_Choice*>( w );
	const Fl_Menu_Item *item = choice->mvalue();
	if( item == nullptr || item->user_data() == nullptr ) return;
	in_variable_selected( (const char *)item->user_data() );
}

namespace {
// FLTK's Fl_Menu_::add(const char*) treats '/' as a submenu path separator
// and '&' as a shortcut-underline marker; escape both so variable names
// containing them (netCDF4 group paths, say) still display as plain text
// in a single flat menu instead of being silently split into submenus.
std::string escapeMenuLabel( const char *name )
{
	std::string out;
	for( const char *p = name; *p; ++p ) {
		if( *p == '/' || *p == '&' ) out += '\\';
		out += *p;
	}
	return out;
}
} // namespace

void MainWindow::populateVarList()
{
	var_pack_->clear();
	var_choices_.clear();

	// Group variables by their number of non-degenerate dimensions, same
	// buckets upstream's x_sort_vars_by_ndims() uses for "menu" var-selection
	// style (1d, 2d, 3d, 4d, 5-or-more), alpha-sorted within each bucket.
	std::vector<NCVar*> buckets[5];
	for( auto &v : variables ) {
		int d = v->effective_dimensionality;
		int idx = ( d >= 1 && d <= 4 ) ? d - 1 : 4;
		buckets[idx].push_back( v.get() );
	}
	for( auto &b : buckets )
		std::sort( b.begin(), b.end(),
			[]( const NCVar *a, const NCVar *c ) { return a->name < c->name; } );

	static const char *kBucketSuffix[5] = { "1d", "2d", "3d", "4d", "5d" };
	for( int i = 0; i < 5; i++ ) {
		if( buckets[i].empty() ) continue;

		auto *choice = new Fl_Choice( 0, 0, var_pack_->w(), 24 );
		char header[64];
		std::snprintf( header, sizeof(header), "(%zu) %s vars", buckets[i].size(), kBucketSuffix[i] );
		choice->add( header, 0, nullptr, nullptr, FL_MENU_INACTIVE );
		for( NCVar *v : buckets[i] ) {
			std::string label = escapeMenuLabel( v->name.c_str() );
			choice->add( label.c_str(), 0, &MainWindow::varChoiceCallback, (void *)v->name.c_str() );
		}
		choice->value( 0 );
		var_pack_->add( choice );
		var_choices_.push_back( choice );
	}
	rebuildColormapChoice();
	var_pack_->redraw();
}

// Replaces the old "Colormap" button-bar button (which just cycled through
// colormaps_ one at a time with no indication of what any of them looked
// like) with a combobox showing every colormap's name plus a small preview
// swatch, so the choice can be made directly instead of by cycling blind.
// Rebuilt alongside the variable-bucket combos above it (populateVarList()
// clears and rebuilds var_pack_ as a whole on every file load), which is
// what makes it move up/down with them: it's simply the last child of the
// same vertical pack, so it sits directly below however many dimensionality
// buckets (1d/2d/3d/4d/5d) the current file's variables happen to produce.
void MainWindow::rebuildColormapChoice()
{
	colormap_choice_ = new Fl_Choice( 0, 0, var_pack_->w(), 24 );
	for( size_t i = 0; i < colormaps_.size(); i++ ) {
		std::string label = escapeMenuLabel( colormaps_[i].name.c_str() );
		int idx = colormap_choice_->add( label.c_str(), 0, &MainWindow::colormapChoiceCallback,
			(void *)(intptr_t)i );
		if( i < colormap_previews_.size() ) {
			auto *item = const_cast<Fl_Menu_Item *>( &colormap_choice_->menu()[idx] );
			// Fl_Menu_Item has no native way to show an icon and text
			// together -- Fl_Image::label(Fl_Menu_Item*) replaces the
			// label with an image-only one, discarding the name text
			// entirely (confirmed: it silently left every item blank).
			// Fl_Multi_Label is FLTK's actual mechanism for pairing an
			// image with text on a menu item. Deliberately heap-allocated
			// and never freed -- this only runs once per file load
			// (populateVarList()), same bounded-leak tradeoff already used
			// for the dim-row callback closures below.
			auto *ml = new Fl_Multi_Label;
			ml->typea = FL_IMAGE_LABEL;
			ml->labela = (const char *)colormap_previews_[i];
			ml->typeb = FL_NORMAL_LABEL;
			ml->labelb = item->label();
			ml->label( item );
		}
	}
	if( current_colormap_ >= 0 && current_colormap_ < (int)colormaps_.size() )
		colormap_choice_->value( current_colormap_ );
	var_pack_->add( colormap_choice_ );
}

void MainWindow::colormapChoiceCallback( Fl_Widget *w, void * )
{
	auto *choice = static_cast<Fl_Choice*>( w );
	const Fl_Menu_Item *item = choice->mvalue();
	if( item == nullptr ) return;
	size_t idx = (size_t)(intptr_t)item->user_data();
	auto *mw = instance();
	if( idx >= mw->colormaps_.size() ) return;
	in_colormap_selected( mw->colormaps_[idx].name.c_str() );
}

void MainWindow::setLabel( Label label_id, const char *s )
{
	int idx = static_cast<int>( label_id );
	if( idx < 0 || idx >= (int)(sizeof(labels_)/sizeof(labels_[0])) ) return;
	if( labels_[idx] == nullptr ) return;
	labels_[idx]->copy_label( s );
	// copy_label() alone doesn't schedule a repaint -- without this, the
	// i/j/value label under the cursor (Label::DataValue, updated on every
	// FL_MOVE via view_report_position()) only appeared to change when some
	// unrelated event happened to trigger a redraw (a click, a resize),
	// making mouse-over tracking look frozen.
	labels_[idx]->redraw();
}

void MainWindow::setSensitive( Button button_id, int state )
{
	// Button::ColormapSelect has no entry in buttons_[] any more (see
	// kButtonSpecs) -- core's set_buttons() still toggles it as part of
	// BUTTONS_ALL_ON/BUTTONS_ALL_OFF, so route it to the combobox instead.
	if( button_id == Button::ColormapSelect ) {
		if( colormap_choice_ ) { if( state ) colormap_choice_->activate(); else colormap_choice_->deactivate(); }
		return;
	}
	int idx = static_cast<int>( button_id );
	if( idx < 0 || idx >= (int)(sizeof(buttons_)/sizeof(buttons_[0])) ) return;
	// A given Button id has either a toolbar button (buttons_) or a menu
	// item (menu_items_), never both -- whichever one this id actually has
	// gets (de)activated, the other slot is just null.
	if( buttons_[idx] != nullptr ) {
		if( state ) buttons_[idx]->activate();
		else buttons_[idx]->deactivate();
	}
	if( menu_items_[idx] != nullptr ) {
		if( state ) menu_items_[idx]->activate();
		else menu_items_[idx]->deactivate();
		// update() (a no-op on non-Mac builds -- see Fl_Menu_Bar::update())
		// is what actually pushes an Fl_Menu_Item flag change like this one
		// out to macOS's native system menu bar; redraw() alone only
		// affects an ordinary in-window Fl_Menu_Bar.
		menu_bar_->update();
		menu_bar_->redraw();
	}
}

void MainWindow::indicateActiveVar( const char *var_name )
{
	for( auto *choice : var_choices_ ) {
		const Fl_Menu_Item *items = choice->menu();
		for( int i = 0; items[i].text != nullptr; i++ ) {
			const char *nm = (const char *)items[i].user_data();
			if( nm && std::strcmp( nm, var_name ) == 0 ) {
				choice->value( i );
				choice->redraw();
				return;
			}
		}
	}
}

namespace {
constexpr int kDimRowNameW = 120, kDimRowBtnW = 24, kDimRowSliderW = 220,
              kDimRowSpacing = 4, kDimRowH = 24;
constexpr int kDimRowContentW = kDimRowNameW + kDimRowSpacing + kDimRowBtnW
                              + kDimRowSpacing + kDimRowSliderW + kDimRowSpacing + kDimRowBtnW;

// Fl_Slider::draw() confines its own label rendering to the knob's small
// rectangle (see the draw_label(xsl,ysl,wsl,hsl) call in FLTK's
// Fl_Slider.cxx) -- fine for a short numeric readout right next to the
// knob, but useless for a full string (a date, a coordinate) that needs
// the whole widget's width to be legible: at a low value the knob is only
// a few pixels wide, clipping the text down to a single character right
// at the widget's left edge. Rather than use label() at all (and get that
// stray knob-confined fragment drawn a second time, in the wrong place),
// this keeps its own display text and draws it centered across the full
// widget instead.
class DimValueSlider : public Fl_Slider {
public:
	DimValueSlider( int X, int Y, int W, int H ) : Fl_Slider( X, Y, W, H ) { type( FL_HOR_SLIDER ); }
	void setDisplayText( const char *s ) { display_text_ = s ? s : ""; redraw(); }
	// Purely diagnostic: which dim row this slider belongs to, used only by
	// handle()'s -debug logging below to tell rows apart in the log.
	void setDimName( const char *s ) { dim_name_ = s ? s : ""; }
	void draw() override
	{
		Fl_Slider::draw();
		fl_push_clip( x(), y(), w(), h() );
		fl_color( active_r() ? labelcolor() : FL_INACTIVE_COLOR );
		fl_font( labelfont(), labelsize() );
		fl_draw( display_text_.c_str(), x(), y(), w(), h(), FL_ALIGN_CENTER );
		fl_pop_clip();
	}
	// Temporary instrumentation for the macOS-only "time" slider bug
	// (doesn't respond to clicks/drags at all on macOS; not reproducible
	// under Linux/Xvfb) -- logs every event this widget actually receives
	// so a `-debug` run on macOS can show whether FL_PUSH/FL_DRAG/
	// FL_RELEASE ever arrive here at all, vs. being eaten by something
	// else (an overlapping widget, a stale Fl::pushed()/belowmouse()
	// pointer, ...) before they reach this handle(). See
	// time_slider_bug_report.md.
	int handle( int event ) override
	{
		if( options.debug )
			fprintf( stderr, "DimValueSlider[%s]::handle: event=%s (%d) at (%d,%d), "
				"pushed()=%p belowmouse()=%p this=%p\n",
				dim_name_.c_str(), fl_eventnames[event], event,
				Fl::event_x(), Fl::event_y(),
				(void*)Fl::pushed(), (void*)Fl::belowmouse(), (void*)this );
		int ret = Fl_Slider::handle( event );
		if( options.debug )
			fprintf( stderr, "DimValueSlider[%s]::handle: event=%s returned %d, value()=%g\n",
				dim_name_.c_str(), fl_eventnames[event], ret, value() );
		return ret;
	}
private:
	std::string display_text_;
	std::string dim_name_;
};
} // namespace

void MainWindow::rebuildDimRow( DimRow &row )
{
	// row.index (this row's stacking position within dim_pack_, set by the
	// caller before this runs) plus dim_pack_'s own x()/y() -- which, being
	// a plain Fl_Widget field, is always current the instant dim_pack_ is
	// resized, unlike a would-be sibling row's position under Fl_Pack's
	// draw()-time relayout -- is what actually places this row, both here
	// and in recenterDimRow() below (called again on every window resize).
	row.group = new Fl_Group( dim_pack_->x(), dim_pack_->y() + row.index*kDimRowH, dim_pack_->w(), kDimRowH );
	row.group->begin();
	row.name_box = new Fl_Box( 0, 0, kDimRowNameW, 22, "" );
	row.name_box->copy_label( row.name.c_str() );
	row.name_box->box( FL_FLAT_BOX );
	row.prev_btn = new Fl_Button( 0, 0, kDimRowBtnW, 22, "@<" );
	row.value_slider = new DimValueSlider( 0, 0, kDimRowSliderW, 22 );
	static_cast<DimValueSlider*>( row.value_slider )->setDimName( row.name.c_str() );
	row.value_slider->box( FL_DOWN_BOX );
	// The slider's own numeric value is just an index into the dimension
	// (bounds/current position set once the dim's size is known, in
	// fillDimInfo() below); what the user actually reads is the formatted
	// value (a date, a coordinate, ...), drawn centered across the full
	// widget by DimValueSlider::draw() above.
	row.value_slider->step( 1 );
	// Fl_Widget's own default is FL_WHEN_RELEASE (see Fl_Widget.cxx) --
	// fine for a text input, wrong for a slider: it made the knob visibly
	// slide under the mouse (that part is handled inside Fl_Slider itself,
	// independent of the callback) while the value text next to it and the
	// displayed 2-D slice both sat frozen on the pre-drag value the whole
	// time, only jumping to the real one on mouse-up -- indistinguishable,
	// mid-drag, from the control not responding at all. FL_WHEN_CHANGED
	// re-reads and redraws on every step instead, same as actually holding
	// prev_btn/next_btn down would.
	row.value_slider->when( FL_WHEN_CHANGED );
	row.next_btn = new Fl_Button( 0, 0, kDimRowBtnW, 22, "@>" );
	row.group->end();
	row.group->resizable( nullptr );  // keep the fixed-size/centered layout on resize; see recenterDimRow()
	dim_pack_->add( row.group );
	recenterDimRow( row );

	// Callback data (dim name + modifier, or just the dim name for the
	// slider) must outlive the callback; heap-allocated and intentionally
	// never freed -- rows are rebuilt only when the scan dimensions
	// change, a rare, low-cardinality event, so this is a small, bounded
	// leak rather than a real one.
	row.prev_btn->callback( &MainWindow::dimStepCallback,
		new std::pair<std::string,Modifier>( row.name, Modifier::M3 ) );
	row.next_btn->callback( &MainWindow::dimStepCallback,
		new std::pair<std::string,Modifier>( row.name, Modifier::M1 ) );
	row.value_slider->callback( &MainWindow::dimSliderCallback, new std::string( row.name ) );
}

// Recomputes this row's absolute position (from dim_pack_'s current x/y/w
// and the row's own index -- never by reading another widget's possibly
// stale position) and re-centers its four children within it. Called once
// at creation (rebuildDimRow() above) and again on every window resize
// (layout() below), since dim_pack_'s width -- and so the centering offset
// -- changes with it.
void MainWindow::recenterDimRow( DimRow &row )
{
	if( row.group == nullptr ) return;
	int row_w = dim_pack_->w();
	row.group->resize( dim_pack_->x(), dim_pack_->y() + row.index*kDimRowH, row_w, kDimRowH );
	int left = ( row_w - kDimRowContentW ) / 2;
	if( left < 0 ) left = 0;
	int x = row.group->x(), y = row.group->y();
	row.name_box->resize( x + left, y, kDimRowNameW, 22 );
	left += kDimRowNameW + kDimRowSpacing;
	row.prev_btn->resize( x + left, y, kDimRowBtnW, 22 );
	left += kDimRowBtnW + kDimRowSpacing;
	row.value_slider->resize( x + left, y, kDimRowSliderW, 22 );
	left += kDimRowSliderW + kDimRowSpacing;
	row.next_btn->resize( x + left, y, kDimRowBtnW, 22 );
}

void MainWindow::dimStepCallback( Fl_Widget *, void *data )
{
	auto *p = static_cast<std::pair<std::string,Modifier>*>(data);
	view_change_cur_dim( (char *)p->first.c_str(), p->second );
}

void MainWindow::dimSliderCallback( Fl_Widget *w, void *data )
{
	auto *name = static_cast<std::string*>(data);
	auto *slider = static_cast<Fl_Slider*>(w);
	view_set_cur_dim_index( name->c_str(), lround( slider->value() ) );
}

void MainWindow::makeDimButtons( const Stringlist *dim_list )
{
	clearDimButtons();
	if( dim_list != nullptr )
	for( auto &e : *dim_list ) {
		DimRow row;
		row.name = e.string;
		row.index = (int)dim_rows_.size();
		rebuildDimRow( row );
		dim_rows_.push_back( row );
	}
	// A plain dim_pack_->redraw() isn't enough -- route through the same
	// full relayout a resize already triggers, which is what actually
	// re-centers every row (recenterDimRow(), called from layout() below)
	// for the current dim_pack_ width.
	layout( win_->w(), win_->h() );

	// Temporary instrumentation for the macOS-only "time" slider bug (see
	// time_slider_bug_report.md and DimValueSlider::handle() above): dump
	// dim_pack_'s actual widget tree after every rebuild, in creation/
	// z-order, so a `-debug` run can confirm there's exactly one row's
	// worth of widgets per dim (no leftover/duplicate group stacked on
	// top of the scan-axis row eating its clicks) and that each row's
	// slider rectangle is where recenterDimRow() thinks it is.
	if( options.debug ) {
		fprintf( stderr, "dim_pack_ children after makeDimButtons(): %d\n", dim_pack_->children() );
		for( int i = 0; i < dim_pack_->children(); ++i ) {
			Fl_Widget *g = dim_pack_->child( i );
			fprintf( stderr, "  [%d] %s %p (%d,%d %dx%d)\n",
				i, typeid(*g).name(), (void*)g,
				g->x(), g->y(), g->w(), g->h() );
			if( auto *grp = dynamic_cast<Fl_Group*>( g ) )
				for( int j = 0; j < grp->children(); ++j ) {
					Fl_Widget *c = grp->child( j );
					fprintf( stderr, "      [%d.%d] %s %p (%d,%d %dx%d)\n",
						i, j, typeid(*c).name(), (void*)c,
						c->x(), c->y(), c->w(), c->h() );
				}
		}
	}
}

void MainWindow::clearDimButtons()
{
	dim_pack_->clear();
	dim_rows_.clear();
}

void MainWindow::fillDimInfo( const NCDim *d, int /*please_flip*/ )
{
	if( d == nullptr ) return;
	for( auto &row : dim_rows_ ) {
		if( row.name == d->name ) {
			row.name_box->copy_label( !d->long_name.empty() ? d->long_name.c_str() : d->name.c_str() );
			row.name_box->redraw();
			// The dim's size is only known once (here, at variable
			// selection/scan-dim-set time), unlike its current place
			// (view_change_cur_dim()/view_set_cur_dim_index()), which
			// changes on every step -- so bounds are set here and the
			// slider's value is kept current in setCurDimValue() below,
			// called every time the place actually changes.
			size_t size = d->size > 0 ? d->size : 1;
			row.value_slider->bounds( 0, (double)(size-1) );
			row.value_slider->value( (double)view_get_cur_dim_index( d->name.c_str() ) );
			break;
		}
	}
}

void MainWindow::setCurDimValue( const char *name, const char *value )
{
	// setDisplayText() below schedules its own redraw() -- see
	// DimValueSlider's comment for why it draws this itself instead of
	// going through label()/copy_label() the way other widgets here do.
	for( auto &row : dim_rows_ ) {
		if( row.name == name ) {
			static_cast<DimValueSlider*>( row.value_slider )->setDisplayText( value );
			row.value_slider->value( (double)view_get_cur_dim_index( name ) );
			return;
		}
	}
}

void MainWindow::indicateActiveDim( Dimension /*dimension*/, const char *dim_name )
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
	colormap_previews_.push_back( buildColormapPreview( r, g, b ) );
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
	if( do_widgets ) {
		setLabel( Label::ColormapName, cm.name.c_str() );
		if( colormap_choice_ ) { colormap_choice_->value( current_colormap_ ); colormap_choice_->redraw(); }
	}
	return (char *)cm.name.c_str();
}

char *MainWindow::installPrevColormap( int do_widgets )
{
	if( colormaps_.empty() ) return nullptr;
	current_colormap_ = (current_colormap_ - 1 + (int)colormaps_.size()) % (int)colormaps_.size();
	auto &cm = colormaps_[current_colormap_];
	image_->setColormap( cm.r, cm.g, cm.b );
	colorbar_->setColormap( cm.r, cm.g, cm.b );
	if( do_widgets ) {
		setLabel( Label::ColormapName, cm.name.c_str() );
		if( colormap_choice_ ) { colormap_choice_->value( current_colormap_ ); colormap_choice_->redraw(); }
	}
	return (char *)cm.name.c_str();
}

char *MainWindow::installColormapByName( const char *name, int do_widgets )
{
	for( size_t i = 0; i < colormaps_.size(); i++ ) {
		if( colormaps_[i].name != name ) continue;
		current_colormap_ = (int)i;
		auto &cm = colormaps_[i];
		image_->setColormap( cm.r, cm.g, cm.b );
		colorbar_->setColormap( cm.r, cm.g, cm.b );
		if( do_widgets ) {
			setLabel( Label::ColormapName, cm.name.c_str() );
			if( colormap_choice_ ) { colormap_choice_->value( current_colormap_ ); colormap_choice_->redraw(); }
		}
		return (char *)cm.name.c_str();
	}
	return nullptr;
}

void MainWindow::createColorbar( float user_min, float user_max, Transform transform )
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

void MainWindow::queryPointerPosition( int *x, int *y ) const
{
	image_->screenToBuffer( Fl::event_x(), Fl::event_y(), x, y );
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
	// Upstream's set_options.c also has a "select which colormaps are
	// enabled for cycling" section, backed by interface/colormap_funcs.c
	// (X11 colorcell allocation, deliberately not ported -- see PORTING.md's
	// M6 notes); everything else there is reproduced here.
	const int kOverlayY = 135;
	int n_overlays = overlay_n_overlays();
	int overlay_bottom = kOverlayY + 20 + n_overlays * 24;

	Fl_Window win( 340, overlay_bottom + 80, "Options" );
	Fl_Check_Button autoscale( 10, 10, 300, 25, "Autoscale each frame" );
	autoscale.value( options.autoscale );
	Fl_Check_Button extra_info( 10, 40, 300, 25, "Show extra info" );
	extra_info.value( options.want_extra_info );
	Fl_Check_Button save_frames( 10, 70, 300, 25, "Save frames in memory" );
	save_frames.value( options.save_frames );
	Fl_Check_Button auto_overlay( 10, 100, 300, 25, "Automatic coastline overlay" );
	auto_overlay.value( options.auto_overlay );

	Fl_Box overlay_label( 10, kOverlayY, 300, 20, "Overlay:" );
	overlay_label.align( FL_ALIGN_LEFT | FL_ALIGN_INSIDE );
	overlay_label.labelfont( FL_HELVETICA_BOLD );

	const char **names = overlay_names();
	int current_overlay = overlay_current();
	int custom_idx = overlay_custom_n();
	std::vector<Fl_Round_Button *> overlay_btns;
	int y = kOverlayY + 20;
	for( int i = 0; i < n_overlays; i++ ) {
		auto *btn = new Fl_Round_Button( 10, y, 300, 22, names[i] );
		btn->type( FL_RADIO_BUTTON );
		if( i == current_overlay ) btn->setonly();
		overlay_btns.push_back( btn );
		y += 24;
	}

	// Upstream's equivalent (set_options.c's static overlay_filename) is
	// also a value that survives across dialog invocations, not reset
	// each time the dialog opens.
	static std::string custom_overlay_filename;
	Fl_Box filename_box( 10, y, 230, 25 );
	filename_box.box( FL_DOWN_BOX );
	filename_box.align( FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP );
	filename_box.copy_label( custom_overlay_filename.empty() ?
		"(no custom overlay file selected)" : custom_overlay_filename.c_str() );
	Fl_Button browse_btn( 250, y, 80, 25, "Browse..." );
	browse_btn.callback( []( Fl_Widget *, void *data ) {
		auto *label_box = static_cast<Fl_Box *>( data );
		Fl_Native_File_Chooser fc;
		fc.title( "Select custom overlay file" );
		char base_dir[1024];
		determine_overlay_base_dir( base_dir, sizeof(base_dir) );
		fc.directory( base_dir );
		if( fc.show() == 0 && fc.filename() != nullptr ) {
			custom_overlay_filename = fc.filename();
			label_box->copy_label( custom_overlay_filename.c_str() );
		}
	}, &filename_box );
	y += 35;

	ModalResult result;
	Fl_Return_Button ok( 100, y, 70, 30, "OK" );
	ok.callback( modalOkCallback, &result );
	Fl_Button cancel( 190, y, 70, 30, "Cancel" );
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

		int new_overlay = current_overlay;
		for( int i = 0; i < n_overlays; i++ )
			if( overlay_btns[i]->value() ) { new_overlay = i; break; }
		// Re-apply if unchanged but "custom": matches upstream's own
		// set_options.c condition, letting a freshly Browse()'d filename
		// take effect even if "Custom" was already selected.
		if( new_overlay != current_overlay || new_overlay == custom_idx )
			do_overlay( new_overlay,
				new_overlay == custom_idx && !custom_overlay_filename.empty() ?
					(char *)custom_overlay_filename.c_str() : nullptr,
				false );

		view_draw( true, false );
	}
}

Message MainWindow::rangeDialog( float old_min, float old_max, float global_min, float global_max,
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

	if( !result.ok ) return Message::Cancel;

	*new_min = (float)atof( min_input.value() );
	*new_max = (float)atof( max_input.value() );
	if( allvars ) *allvars = all_vars_cb.value();
	return Message::OK;
}

int MainWindow::scanDimsDialog( const Stringlist *dim_list, const char *x_axis_name, const char *y_axis_name,
		Stringlist **new_dim_list )
{
	std::vector<std::string> names;
	if( dim_list != nullptr )
		for( auto &e : *dim_list )
			names.push_back( e.string );
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
	stringlist_add_string( new_dim_list, names[y_choice.value()].c_str() );
	stringlist_add_string( new_dim_list, names[x_choice.value()].c_str() );
	return 1;
}

Message MainWindow::printerOptionsDialog( PrintOptions *po )
{
	Fl_Window win( 420, 300, "Printer Options" );
	char buf[64];

	Fl_Box dev_label( 10, 10, 60, 25, "Device:" );
	Fl_Round_Button dev_printer( 80, 10, 90, 25, "Printer" );
	Fl_Round_Button dev_file( 175, 10, 70, 25, "File" );
	dev_printer.type( FL_RADIO_BUTTON );
	dev_file.type( FL_RADIO_BUTTON );
	(po->output_device == Device::Printer ? dev_printer : dev_file).setonly();
	Fl_Input outfile_input( 250, 10, 160, 25 );
	outfile_input.value( po->out_file_name.c_str() );

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
	font_name_input.value( po->font_name.c_str() );
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

	if( !result.ok ) return Message::Cancel;

	po->output_device = dev_printer.value() ? Device::Printer : Device::File;
	po->out_file_name = outfile_input.value();
	po->page_x_margin = (float)atof( xmar_input.value() );
	po->page_upper_y_margin = (float)atof( ytmar_input.value() );
	po->page_lower_y_margin = (float)atof( ybmar_input.value() );
	po->font_name = font_name_input.value();
	po->font_size = atoi( fontsize_input.value() );
	po->header_font_size = atoi( headsize_input.value() );
	po->include_title = include_title.value();
	po->include_axis_labels = include_axis.value();
	po->include_extra_info = include_extra.value();
	po->include_outline = include_outline.value();
	po->include_id = include_id.value();
	po->test_only = test_only.value();

	return Message::OK;
}

} // namespace ncview_ui

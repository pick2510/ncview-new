#include "ncview_ui/main_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Float_Input.H>
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
	int width = w() > 0 ? w() : 1;
	for( int px = 0; px < w(); px++ ) {
		// Mirrors util.cc:data_to_pixels' pixel-index formula exactly
		// (transform, then invert_colors, then scale by n_colors) --
		// upstream's cbar.c does the same in cbar_make(). Without this the
		// colorbar shows a plain linear gradient that no longer matches
		// the image whenever a transform or "Invert Colormap" is active.
		double normval = (double)px / (double)width;
		switch( transform_ ) {
			case TRANSFORM_HI:     normval = normval*normval*normval*normval; break;
			case TRANSFORM_LOW:    normval = sqrt( sqrt( normval ) ); break;
			case TRANSFORM_CENTER: normval = atan( (normval-0.5)*8.0 )/3.1415926536 + 0.5; break;
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

	labels_[LABEL_TITLE]        = new Fl_Box( 10, 5, W-20, 20 );
	labels_[LABEL_SCANVAR_NAME] = new Fl_Box( 10, 25, 200, 18 );
	labels_[LABEL_SCAN_PLACE]   = new Fl_Box( 220, 25, 200, 18 );
	labels_[LABEL_DATA_EXTREMA] = new Fl_Box( 10, 43, 300, 18 );
	// Wide enough to actually show the full "Current: (i=.., j=..) val
	// (x=.., y=..)" string view_report_position() builds -- the old fixed
	// 200px width clipped it right after "(x=", silently hiding the x/y
	// coordinate values even though they were always part of the label
	// text and updating on every mouse move. Resized to track the window
	// edge in layout() below, same as LABEL_TITLE.
	labels_[LABEL_DATA_VALUE]   = new Fl_Box( 320, 43, W-330, 18 );
	labels_[LABEL_COLORMAP_NAME]= new Fl_Box( 10, 61, 150, 18 );
	// No LABEL_BLOWUP ("M X<n>") box -- it showed core's discrete
	// pre-zoom pixel-buffer scale factor, which upstream's now-removed
	// BUTTON_BLOWUP let you cycle. With that gone (replaced by ImageView's
	// continuous scroll/drag zoom, which this label never reflected anyway)
	// it was a static, unexplained number nobody could act on.
	labels_[LABEL_TRANSFORM]    = new Fl_Box( 170, 61, 80, 18 );
	labels_[LABEL_BLOWUP_TYPE]  = new Fl_Box( 260, 61, 100, 18 );
	labels_[LABEL_CCINFO_1]     = new Fl_Box( 370, 61, 200, 18 );
	labels_[LABEL_CCINFO_2]     = new Fl_Box( 370, 79, 200, 18 );
	labels_[LABEL_SKIP]         = new Fl_Box( 10, 79, 150, 18 );
	labels_[LABEL_SCALAR_DIMS]  = new Fl_Box( 170, 79, 280, 18 );
	for( auto *b : labels_ ) if( b ) { b->box( FL_NO_BOX ); b->align( FL_ALIGN_INSIDE | FL_ALIGN_LEFT ); }

	var_pack_ = new Fl_Pack( 10, 100, 180, H-220 );
	var_pack_->type( Fl_Pack::VERTICAL );
	var_pack_->spacing( 2 );
	var_pack_->end();

	image_ = new ImageView( 200, 100, W-210, H-320 );
	colorbar_ = new Colorbar( 200, H-210, W-210, 20 );

	dim_pack_ = new Fl_Pack( 10, H-180, W-20, 100 );
	dim_pack_->type( Fl_Pack::VERTICAL );
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
	const int kTopY = 100;
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
	dim_pack_->resize( kSideMargin, dim_pack_y, w - 2*kSideMargin, kDimPackH );
	colorbar_->resize( kImageX, colorbar_y, right_w, kColorbarH );
	image_->resize( kImageX, kTopY, right_w, image_h );

	int var_pack_h = dim_pack_y - kVarPackGap - kTopY;
	if( var_pack_h < 40 ) var_pack_h = 40;
	var_pack_->resize( kSideMargin, kTopY, 180, var_pack_h );

	if( labels_[LABEL_TITLE] ) labels_[LABEL_TITLE]->size( w - 2*kSideMargin, labels_[LABEL_TITLE]->h() );
	if( labels_[LABEL_DATA_VALUE] ) {
		int lx = labels_[LABEL_DATA_VALUE]->x();
		labels_[LABEL_DATA_VALUE]->size( w - kSideMargin - lx, labels_[LABEL_DATA_VALUE]->h() );
	}

	win_->redraw();
}

// Explicit per-button pixel widths (rather than one fixed size for all)
// since a uniform 60px was too narrow for "Transform"/etc, leaving their
// labels crowding the button edges.
//
// No BUTTON_COLORMAP_SELECT entry here -- replaced by the colormap combobox
// in var_pack_ (see rebuildColormapChoice()), which shows every colormap's
// name and a preview swatch instead of cycling through them blind one at a
// time. BUTTON_COLORMAP_SELECT/do_colormap_sel() still exist for the
// NCVIEW_TEST_BUTTON headless-test hook and any script driving buttons by
// id directly; they just have no on-screen button anymore.
struct ButtonSpec { int id; const char *text; int width; };
static const ButtonSpec kButtonSpecs[] = {
	{ BUTTON_REWIND, "@|<", 40 }, { BUTTON_BACKWARDS, "@<", 40 }, { BUTTON_PAUSE, "@||", 40 },
	{ BUTTON_FORWARD, "@>", 40 }, { BUTTON_FASTFORWARD, "@>|", 40 }, { BUTTON_RESTART, "Restart", 65 },
	{ BUTTON_INVERT_PHYSICAL, "Inv.Phys", 75 },
	{ BUTTON_INVERT_COLORMAP, "Inv.Cmap", 78 }, { BUTTON_MINIMUM, "Min", 50 }, { BUTTON_MAXIMUM, "Max", 50 },
	// No BUTTON_BLOWUP here -- replaced by ImageView's scroll-to-zoom (mouse
	// wheel) and drag-to-pan (left-button drag), which give continuous
	// navigation instead of upstream's discrete button. BUTTON_BLOWUP_TYPE
	// is unrelated to navigation (it toggles how core resamples pixels --
	// replicate vs bilinear -- independent of the on-screen zoom level), so
	// unlike BUTTON_BLOWUP it still needs a real control; its current state
	// is shown by the passive LABEL_BLOWUP_TYPE box up in the info area
	// (unchanged from upstream), this button is just the trigger.
	{ BUTTON_BLOWUP_TYPE, "Interp", 60 },
	{ BUTTON_TRANSFORM, "Transform", 85 },
	{ BUTTON_DIMSET, "DimSet", 65 }, { BUTTON_RANGE, "Range", 60 }, { BUTTON_EDIT, "Edit", 50 },
	{ BUTTON_INFO, "Info", 50 }, { BUTTON_PRINT, "Print", 55 }, { BUTTON_OPTIONS, "Options", 70 },
	{ BUTTON_QUIT, "Quit", 55 },
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

	for( const auto &spec : kButtonSpecs ) {
		int with_this = row_width + ( row_width > 0 ? kSpacing : 0 ) + spec.width;
		if( row == nullptr || with_this > available_width ) {
			row = new Fl_Pack( 0, 0, available_width, kButtonHeight );
			row->type( Fl_Pack::HORIZONTAL );
			row->spacing( kSpacing );
			button_bar_->add( row );
			row_width = 0;
			n_rows++;
		}
		auto *btn = new Fl_Button( 0, 0, spec.width, kButtonHeight, spec.text );
		btn->callback( &MainWindow::buttonCallback, (void*)(intptr_t)spec.id );
		row->add( btn );
		buttons_[spec.id] = btn;
		row_width += ( row_width > 0 ? kSpacing : 0 ) + spec.width;
	}

	return n_rows*kButtonHeight + ( n_rows > 0 ? (n_rows-1)*kSpacing : 0 );
}

void MainWindow::buttonCallback( Fl_Widget *, void *data )
{
	int id = (int)(intptr_t)data;
	in_button_pressed( id, MOD_1 );
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
	in_variable_selected( (char *)item->user_data() );
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
	for( NCVar *v = variables; v != nullptr; v = (NCVar *)v->next ) {
		int d = v->effective_dimensionality;
		int idx = ( d >= 1 && d <= 4 ) ? d - 1 : 4;
		buckets[idx].push_back( v );
	}
	for( auto &b : buckets )
		std::sort( b.begin(), b.end(),
			[]( const NCVar *a, const NCVar *c ) { return std::strcmp( a->name, c->name ) < 0; } );

	static const char *kBucketSuffix[5] = { "1d", "2d", "3d", "4d", "5d" };
	for( int i = 0; i < 5; i++ ) {
		if( buckets[i].empty() ) continue;

		auto *choice = new Fl_Choice( 0, 0, var_pack_->w(), 24 );
		char header[64];
		std::snprintf( header, sizeof(header), "(%zu) %s vars", buckets[i].size(), kBucketSuffix[i] );
		choice->add( header, 0, nullptr, nullptr, FL_MENU_INACTIVE );
		for( NCVar *v : buckets[i] ) {
			std::string label = escapeMenuLabel( v->name );
			choice->add( label.c_str(), 0, &MainWindow::varChoiceCallback, (void *)v->name );
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
	in_colormap_selected( (char *)mw->colormaps_[idx].name.c_str() );
}

void MainWindow::setLabel( int label_id, const char *s )
{
	if( label_id < 0 || label_id >= (int)(sizeof(labels_)/sizeof(labels_[0])) ) return;
	if( labels_[label_id] == nullptr ) return;
	labels_[label_id]->copy_label( s );
	// copy_label() alone doesn't schedule a repaint -- without this, the
	// i/j/value label under the cursor (LABEL_DATA_VALUE, updated on every
	// FL_MOVE via view_report_position()) only appeared to change when some
	// unrelated event happened to trigger a redraw (a click, a resize),
	// making mouse-over tracking look frozen.
	labels_[label_id]->redraw();
}

void MainWindow::setSensitive( int button_id, int state )
{
	// BUTTON_COLORMAP_SELECT has no entry in buttons_[] any more (see
	// kButtonSpecs) -- core's set_buttons() still toggles it as part of
	// BUTTONS_ALL_ON/BUTTONS_ALL_OFF, so route it to the combobox instead.
	if( button_id == BUTTON_COLORMAP_SELECT ) {
		if( colormap_choice_ ) { if( state ) colormap_choice_->activate(); else colormap_choice_->deactivate(); }
		return;
	}
	if( button_id < 0 || button_id >= (int)(sizeof(buttons_)/sizeof(buttons_[0])) ) return;
	if( buttons_[button_id] == nullptr ) return;
	if( state ) buttons_[button_id]->activate();
	else buttons_[button_id]->deactivate();
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
	// A plain dim_pack_->redraw() isn't enough: Fl_Pack::draw() only
	// recomputes child positions using the *pack's own* damage state, and
	// after clear()+add() that alone doesn't reliably repaint a newly added
	// row at the bottom (confirmed empirically -- switching to a variable
	// with more scannable dims than the previous one, e.g. a 2-D var to a
	// 3-D one, silently drops the last dimension row until some unrelated
	// event forces a real relayout, such as resizing the window). Route
	// through the same full relayout a resize already triggers instead.
	layout( win_->w(), win_->h() );
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
			row.name_box->redraw();
			break;
		}
	}
}

void MainWindow::setCurDimValue( const char *name, const char *value )
{
	// Same missing-repaint bug as MainWindow::setLabel() (see its comment):
	// copy_label() alone doesn't schedule a redraw, so a dimension row's
	// displayed value -- e.g. "Time" during animation playback or manual
	// scrubbing with the row's prev/next buttons -- only appeared to update
	// when some unrelated event happened to repaint the window.
	for( auto &row : dim_rows_ ) {
		if( row.name == name ) {
			row.value_box->copy_label( value );
			row.value_box->redraw();
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
		setLabel( LABEL_COLORMAP_NAME, cm.name.c_str() );
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
		setLabel( LABEL_COLORMAP_NAME, cm.name.c_str() );
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
			setLabel( LABEL_COLORMAP_NAME, cm.name.c_str() );
			if( colormap_choice_ ) { colormap_choice_->value( current_colormap_ ); colormap_choice_->redraw(); }
		}
		return (char *)cm.name.c_str();
	}
	return nullptr;
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

	char **names = overlay_names();
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

	if( !result.ok ) return Message::Cancel;

	po->output_device = dev_printer.value() ? Device::Printer : Device::File;
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

	return Message::OK;
}

} // namespace ncview_ui

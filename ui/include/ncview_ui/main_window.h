/*
 * ui/include/ncview_ui/main_window.h
 *
 * The FLTK implementation of ncview's main window. This is the M3 rewrite
 * of upstream's src/interface/x_interface.c (Xt/Xaw) -- same job (own the
 * 2-D field display, colorbar, button bar, labels, dimension controls,
 * variable selector), different toolkit. It is driven entirely through the
 * functions declared in ncview/interface.h (see ui/src/interface_fltk.cc,
 * which is the thin free-function layer FLTK callbacks and ncview_core call
 * into, delegating to this class).
 */
#pragma once

#include <string>
#include <vector>

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Widget.H>

// This whole project is C++ throughout (core included), so these are
// ordinary C++-linkage includes -- no extern "C" needed, and it would be
// actively wrong here since in_timer_set() takes a std::function.
#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

namespace ncview_ui {

// Displays the 2-D color-contour field. Owns nothing about the data; it's
// handed a fresh ncv_pixel buffer plus a shared 256-entry RGB colormap
// table each time core calls in_draw_2d_field().
class ImageView : public Fl_Widget {
public:
	ImageView( int x, int y, int w, int h );
	~ImageView() override;

	void setColormap( const unsigned char *r, const unsigned char *g, const unsigned char *b );
	void setData( const unsigned char *data, size_t width, size_t height );
	void draw() override;
	int handle( int event ) override;

private:
	unsigned char colormap_r_[256], colormap_g_[256], colormap_b_[256];
	std::vector<unsigned char> pixels_;   // index buffer, width_*height_
	std::vector<unsigned char> rgb_buf_;  // expanded RGB buffer, width_*height_*3
	size_t width_ = 0, height_ = 0;
};

// A simple horizontal gradient strip showing the current colormap over the
// data's [user_min, user_max] range. Upstream's cbar.c built a similarly
// simple strip (plus tick labels drawn elsewhere); this is the M3 minimum
// -- richer tick/label rendering is M4 polish.
class Colorbar : public Fl_Widget {
public:
	Colorbar( int x, int y, int w, int h );

	void setColormap( const unsigned char *r, const unsigned char *g, const unsigned char *b );
	void setRange( float user_min, float user_max, int transform );
	void draw() override;

private:
	unsigned char colormap_r_[256], colormap_g_[256], colormap_b_[256];
	float user_min_ = 0.f, user_max_ = 1.f;
	int transform_ = TRANSFORM_NONE;
};

struct DimRow {
	std::string name;
	Fl_Box     *name_box   = nullptr;
	Fl_Box     *value_box  = nullptr;
	Fl_Button  *prev_btn   = nullptr;
	Fl_Button  *next_btn   = nullptr;
};

struct NamedColormap {
	std::string name;
	unsigned char r[256], g[256], b[256];
};

class MainWindow {
public:
	MainWindow();

	Fl_Double_Window *window() { return win_; }

	// ---- ncview/interface.h contract -------------------------------
	void setLabel( int label_id, const char *s );
	void setSensitive( int button_id, int state );
	void indicateActiveVar( const char *var_name );
	void indicateActiveDim( int dimension, const char *dim_name );
	void makeDimButtons( Stringlist *dim_list );
	void clearDimButtons();
	void fillDimInfo( NCDim *d, int please_flip );
	void setCurDimValue( const char *name, const char *value );
	void draw2DField( const unsigned char *data, size_t width, size_t height, size_t timestep );
	void createColormap( const char *name, const unsigned char *r, const unsigned char *g, const unsigned char *b );
	int  set2DSize( size_t width, size_t height );
	char *installNextColormap( int do_widgets );
	char *installPrevColormap( int do_widgets );
	bool  seenColormapName( const char *name ) const;
	void  checkLegalColormapLoaded();
	void  createColorbar( float user_min, float user_max, int transform );
	void  drawColorbar();
	void  populateVarList();
	void  setCursorBusy( bool busy );
	void  pixelToRgb( ncv_pixel pix, int *r, int *g, int *b ) const;

	// ---- M4 dialogs -------------------------------------------------
	void setOptionsDialog();
	int  rangeDialog( float old_min, float old_max, float global_min, float global_max,
			float *new_min, float *new_max, int *allvars );
	int  scanDimsDialog( Stringlist *dim_list, char *x_axis_name, char *y_axis_name,
			Stringlist **new_dim_list );
	int  printerOptionsDialog( PrintOptions *po );

private:
	void rebuildButtonBar();
	void rebuildDimRow( DimRow &row );
	static void buttonCallback( Fl_Widget *w, void *data );
	static void varBrowserCallback( Fl_Widget *w, void *data );
	static void dimStepCallback( Fl_Widget *w, void *data );

	Fl_Double_Window *win_ = nullptr;
	ImageView         *image_ = nullptr;
	Colorbar          *colorbar_ = nullptr;
	Fl_Pack           *button_bar_ = nullptr;
	Fl_Pack           *dim_pack_ = nullptr;
	Fl_Hold_Browser   *var_browser_ = nullptr;
	Fl_Box            *labels_[16] = {};          // indexed by LABEL_*
	Fl_Widget         *buttons_[32] = {};          // indexed by BUTTON_*

	std::vector<DimRow> dim_rows_;
	std::vector<NamedColormap> colormaps_;
	int current_colormap_ = -1;
};

// The single MainWindow instance; created in in_initialize(), used by every
// free function in interface_fltk.cc.
MainWindow *instance();

} // namespace ncview_ui

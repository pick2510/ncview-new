/*
 * core/include/ncview/interface.h
 *
 * The toolkit seam. ncview_core calls only the functions declared here to
 * talk to the UI; ncview_ui (FLTK) implements every one of them. This is
 * upstream's in_* contract (originally declared inline in ncview.protos.h,
 * implemented by src/interface/interface.c delegating to x_interface.c),
 * plus a handful of functions core calls directly by name that are really
 * UI dialogs/state (set_options, printer_options, x_range, x_dataedit,
 * x_seen_colormap_name, x_check_legal_colormap_loaded, x_create_colorbar,
 * x_draw_colorbar, x_error, x_force_set_invert_state, x_init_dim_info,
 * x_set_var_sensitivity, get_persistent_X_state, unlock_plot) -- upstream
 * never routed those through in_*, but they are exactly as much a part of
 * the seam. See PORTING.md, "Why the port is tractable".
 *
 * A headless implementation of everything in this file lives in
 * tests/stub_interface.cc and is what proves ncview_core has no hidden UI
 * dependency.
 */
#pragma once

#include <functional>

#include "ncview/stringlist.h"

/* NCVar, NCDim, ncv_pixel, and PrintOptions come from ncview/defines.h,
 * which every translation unit that reaches this header includes first
 * (the upstream convention: includes.h, then defines.h, then protos.h,
 * which pulls in this file). Not forward-declared here: they are anonymous
 * struct typedefs in defines.h, so a "struct NCVar;" forward declaration
 * here would name an unrelated, incompatible type. */

/******************************************************************************
 * in_* : implemented by src/interface/interface.c upstream, now by ncview_ui.
 *
 * Not here (moved to ncview/protos.h + core/src/interface_glue.cc instead):
 * in_variable_selected, in_button_pressed, in_error -- core itself calls
 * these, so they can't be things only ncview_ui implements. Also not here
 * (upstream had them as trivial one-line forwards to a *_core* function;
 * ncview_ui just calls that core function directly instead): report_position
 * (-> view_report_position), in_change_dat (-> view_change_dat),
 * in_data_edit_dump (-> view_data_edit_dump), in_change_current
 * (-> view_change_cur_dim). Also not here: in_make_dim_buttons and
 * in_clear_dim_buttons -- declared upstream but never actually called from
 * any core file (dead prototypes, like clip_i()); the real dimension-panel
 * entry points core uses are x_init_dim_info() and in_fill_dim_info(),
 * both below.
 */
void 	in_display_stuff	( char *s, char *var_name );
void 	in_set_edit_place	( size_t index, int x, int y, int nx, int ny );
void 	in_indicate_active_var  ( char *var_name );
void 	in_indicate_active_dim  ( int dimension, char *dim_name );
void 	in_parse_args		( int *p_argc, char **argv );
void 	in_initialize		( void );
void 	in_set_label		( int label_id, char *string );
void	in_process_user_input	( void );
void	in_draw_2d_field 	( unsigned char *data, size_t width, size_t height, size_t timestep );
void	in_create_colormap	( char *name, ncv_pixel r[256], ncv_pixel g[256], ncv_pixel b[256] );
char	*in_install_next_colormap( int do_widgets_flag );
int	in_set_2d_size   	( size_t width, size_t height );
void	in_set_sensitive	( int button_id, int state );
int	in_dialog		( char *message, char *ret_string, int want_cancel_button );
void 	in_var_set_sensitive	( char *var_name, int sensitivity );
void 	in_fill_dim_info	( NCDim *d, int please_flip );
void	in_set_cur_dim_value	( char *name, char *string );
void 	in_set_cursor_busy	( void );
void 	in_set_cursor_normal	( void );
int 	in_set_scan_dims	( Stringlist *dim_list, char *x_axis, char *y_axis, Stringlist **new_dim_list );
void	in_change_min		( char *label );
void 	in_flush		( void );
int	in_popup_XY_graph	( size_t n, int dimindex, double *xvals, double *yvals, char *x_axis_title,
				char *y_axis_title, char *title, char *legend,
				Stringlist *scannable_dims );
void 	in_query_pointer_position( int *x, int *y );
void	in_popup_2d_window	( void );
void	in_popdown_2d_window	( void );
void 	in_timer_clear		( void );
int	in_report_auto_overlay  ( void );
/* Upstream signature took an Xt XtTimerCallbackProc + XtPointer; this port
 * uses std::function so the seam has no toolkit type in it. */
void 	in_timer_set            ( std::function<void()> callback, unsigned long delay_millisec );
char    *in_install_prev_colormap( int do_widgets );

/******************************************************************************
 * Functions core calls directly (not via in_*) that are nonetheless UI
 * dialogs/state, implemented by ncview_ui.
 */
void	set_options		( void );
int	printer_options		( PrintOptions *po );
void	printer_options_init	( void );
int	x_range( float old_min, float old_max, float global_min, float global_max,
		float *new_min, float *new_max, int *allvars );
void	x_dataedit( char **text, int nx );
int	x_seen_colormap_name( char *name );
void	x_check_legal_colormap_loaded( void );
void	x_create_colorbar( float user_min, float user_max, int transform );
void	x_draw_colorbar( void );
void	x_error( char *message );
void	x_force_set_invert_state( int state );
void	x_init_dim_info( Stringlist *dim_list );
void	x_set_var_sensitivity( char *varname, int sens );
void	unlock_plot( void );
Stringlist *get_persistent_X_state( void );
void	pix_to_rgb( ncv_pixel pix, int *r, int *g, int *b );

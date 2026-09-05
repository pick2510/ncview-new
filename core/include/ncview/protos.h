/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 1993-2024 David W. Pierce
 *
 * This program  is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License, version 3, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * David W. Pierce
 * davidwilliampierce@gmail.com
 */

/*
 * core/include/ncview/protos.h
 *
 * Prototypes for the UI-free ncview_core library. This is upstream's
 * ncview.protos.h with every UI-implementation-only prototype (x_interface.c,
 * range.c, set_options.c, filesel.c, plot_xy.c, plot_range.c, x_dataedit.c,
 * x_display_info.c, printer_options.c, interface/cbar.c, interface/make_tc_data.c,
 * interface/colormap_funcs.c) removed, EXCEPT for the small subset of those
 * functions core actually calls directly by name (not through the in_*
 * contract) -- those are declared below in "interface.h" as part of the seam
 * ncview_ui must implement. See PORTING.md.
 */

#pragma once

#include "ncview/stringlist.h"
#include "ncview/interface.h"

/* Global state, defined in ncview.cc. Upstream had every .c file that
 * needed these declare its own local `extern`; this is the one canonical
 * declaration ncview_ui can use too. */
extern Options options;
extern NCVar   *variables;

/******************************************************************************
 * in ncview.c
 */
/* Upstream's main(); renamed so ncview_ui's app/main.cc can be the real
 * process entry point (and so this library never defines `main` itself). */
int	ncview_main		    ( int argc, char **argv );
void	initialize_misc		    ( void );
Stringlist *parse_options           ( int argc,  char *argv[] );
void 	initialize_file_interface   ( Stringlist *input_files );
void	initialize_display_interface( void );
void	initialize_colormaps	    ( void );
void	init_cmap_from_file	    ( char *dir_name, char *file_name, int n_suffix );
void	process_user_input          ( void );
void	quit_app		    ( void );
void	create_default_colormap     ( void );
int	check			    ( int value, int min, int max );
void	print_disclaimer	    ( void );
void	print_no_warranty	    ( void );
void	print_copying	    	    ( void );
void	useage			    ( void );

/******************************************************************************
 * in file.c
 */
int 	fi_confirm       ( char *name );
int	fi_writable      ( char *name );
int 	fi_initialize    ( char *name, int nfiles );
Stringlist *fi_list_vars ( int fileid );
int	fi_n_dims	 ( int fileid, char *var_name );
size_t	*fi_var_size	 ( int fileid, char *var_name );
void 	fi_get_data      ( NCVar *var, size_t *start_pos, size_t *count, void *data );
void 	fi_close         ( int fileid );
void	determine_file_type( Stringlist *input_files );
Stringlist *fi_scannable_dims( int fileid, char *var_name );
char 	*fi_title        ( int fileid );
char 	*fi_long_var_name( int fileid, char *var_name );
char 	*fi_var_units    ( int fileid, char *var_name );
char 	*fi_dim_units    ( int fileid, char *var_name );
char 	*fi_dim_calendar ( int fileid, char *dim_name );
int 	fi_has_dim_values( int fileid, char *dim_name );
char 	*fi_dim_longname ( int fileid, char *dim_name );
nc_type fi_dim_value     ( NCVar *v, int dim_id, size_t place, double *ret_val_double, char *ret_val_char,
				int *return_has_bounds, double *return_bounds_min, double *return_bounds_max,
				size_t *complete_ndim_virt_place );
char 	*fi_dim_id_to_name( int fileid, char *var_name, int dim_id );
int 	fi_dim_name_to_id( int fileid, char *var_name, char *dim_name );
size_t 	fi_n_dim_entries ( int fileid, char *dim_name );
void 	fi_fill_aux_data ( int id, char *var_name, FDBlist *fdb );
void 	fi_fill_value	 ( NCVar *var, float *fillval );
int 	fi_recdim_id     ( int fileid );

/******************************************************************************
 * in file_netcdf.c, netcdf specific routines
 */
char *  netcdf_att_string       ( int fileid, char *var_name );
char *  netcdf_global_att_string( int fileid );
int 	netcdf_fi_confirm	( char *name );
int 	netcdf_fi_writable	( char *name );
int 	netcdf_fi_initialize	( char *name );
Stringlist *netcdf_fi_list_vars	( int fileid );
int	netcdf_fi_n_dims	( int fileid, char *var_name );
size_t	*netcdf_fi_var_size	( int fileid, char *var_name );
void 	netcdf_fi_get_data	( int fileid, char *var_name, size_t *start_pos,
						size_t *count, float *data, NetCDFOptions *aux_data );
void	netcdf_fi_close		( int fileid );
int 	netcdf_n_dims 		( int cdfid, char *varname );
char	*netcdf_varindex_to_name( int cdfid, int index );
Stringlist *netcdf_scannable_dims( int fileid, char *var_name );
char 	*netcdf_title           ( int fileid );
char 	*netcdf_long_var_name   ( int fileid, char *var_name );
char 	*netcdf_get_char_att( int fileid, char *var_name, char *att_name );
char 	*netcdf_var_units       ( int fileid, char *var_name );
char 	*netcdf_dim_units       ( int fileid, char *var_name );
int 	netcdf_has_dim_values   ( int fileid, char *dim_name );
char 	*netcdf_dim_longname 	( int fileid, char *dim_name );
nc_type	netcdf_dim_value     	( int fileid, char *dim_name, size_t place, double *ret_val_double, char *ret_val_char,
				  size_t virt_place, int *has_bounds, double *return_bounds_min, double *return_bounds_max  );
char 	*netcdf_dim_id_to_name  ( int fileid, char *var_name, int dim_id );
int 	netcdf_dim_name_to_id   ( int fileid, char *var_name, char *dim_name );
size_t 	netcdf_n_dim_entries    ( int fileid, char *dim_name );
void 	netcdf_fill_aux_data    ( int id, char *var_name, FDBlist *fdb );
int	netcdf_min_max_option_set( NCVar *var, float *ret_min, float *ret_max );
int	netcdf_min_option_set	( NCVar *var, float *ret_min );
int	netcdf_max_option_set	( NCVar *var, float *ret_max );
void 	netcdf_fill_value	( int file_id, char *var_name, float *v, NetCDFOptions *opts );
int 	netcdf_fi_recdim_id     ( int fileid );
int 	netcdf_dimvar_bounds_id ( int fileid, char *dim_name, int *nvertices );
char 	*netcdf_dim_calendar( int fileid, char *dim_name );
int 	safe_ncvarid( int fileid, char *varname );

/******************************************************************************
 * in util.c, general utility routines
 */
int 	close_enough	   ( float data, float fill );
void 	new_fdblist        ( FDBlist **el );
void 	new_netcdf         ( NetCDFOptions **n );
int	data_to_pixels     ( View *v );
void	add_var_to_list    ( char *var_name, int file_id, char *filename, int nfiles );
NCVar	*get_var	   ( char *var_name );
void	add_to_varlist     ( NCVar **list, NCVar *new_var );
void	init_min_max	   ( NCVar *var );
void	clip_f		   ( float *val, float min, float max );
void	clip_i		   ( int   *val, int   min, int   max );
void 	fill_dim_structs   ( NCVar *v );
void 	expand_data	   ( float *big_data, View *v, size_t array_size );
void 	check_ranges       ( NCVar *var );
char 	*limit_string	   ( char *s );
int 	*gen_overlay       ( View *v, char *overlay_fname );
void 	fmt_time	   ( char *temp_string, size_t temp_string_len, double new_dimval, NCDim *dim, int include_granularity );
int	n_vars_in_list	   ( NCVar *v );
void 	set_blowup_type	   ( int new_type );
int 	n_strings_in_list  ( Stringlist *s );
int 	strncmp_nocase     ( char *s1, char *s2, size_t n );
int 	warn_if_file_exits ( char *fname );
void 	virt_to_actual_place( NCVar *var, size_t *virt_pl, size_t *act_pl, FDBlist **file );
void 	calc_dim_minmaxes   ( void );
void    add_vars_to_list    ( Stringlist *var_list, int id, char *filename, int nfiles );
int     is_scannable        ( NCVar *v, int i );
void 	sl_cat		    ( Stringlist **dest, Stringlist **src );
void 	get_min_max_onestep( NCVar *var, size_t n_other, size_t tstep, float *data,
					float *min, float *max, int verbose );
int 	unpack_groupname( char *varname, int ig, char *groupname );
void 	cache_scalar_coord_info( NCVar *vars );
int 	count_nslashes	    ( char *s );
Stringlist *get_group_list  ( NCVar *vars );
void 	varname_no_groups   ( char *varname, char *varname_sans_groups, char *groupname );
unsigned char interp( int i, int range_i, unsigned char *mat, int n_entries );

/******************************************************************************
 * in do_buttons.c
 */
int 	which_button_pressed( void );
void 	do_range 	  ( int modifier );
void 	do_quit		  ( int modifier );
void 	do_data_edit	  ( int modifier );
void 	do_info		  ( int modifier );
void 	do_options        ( int modifier );
void 	do_dimset         ( int modifier );
void	do_restart        ( int modifier );
void	do_rewind         ( int modifier );
void	do_backwards      ( int modifier );
void	do_pause          ( int modifier );
void	do_forward        ( int modifier );
void	do_fastforward    ( int modifier );
void	do_colormap_sel   ( int modifier );
void	do_invert_physical( int modifier );
void	do_invert_colormap( int modifier );
void	do_set_minimum    ( int modifier );
void	do_set_maximum    ( int modifier );
void	do_blowup	  ( int modifier );
void	do_transform	  ( int modifier );
void	do_blowup_type	  ( int modifier );

/******************************************************************************
 * in view.c
 */
int 	set_scan_variable    ( NCVar *var );
void 	set_scan_view        ( size_t scan_place );
int 	change_view          ( int delta, int interpretation );
int	view_draw            ( int allow_saveframes_useage, int force_range_to_frame );
void 	view_change_cur_dim  ( char *dim_name, int modifier );
void	view_forward         ( void );
void	view_backward        ( void );
void	view_change_blowup   ( int delta, int redraw_flag, int view_var_is_valid );
void	init_saveframes	     ( void );
void 	redraw_dimension_info( void );
void 	redraw_ccontour      ( void );
void	view_check_new_data  ( int unused );
void	view_report_position ( int x, int y, unsigned int button_mask );
void 	view_report_position_vals( float xval, float yval, int plot_index );
void 	plot_XY              ( void );
void 	set_dataedit_place   ( void );
void    view_data_edit_dump  ( void );
void 	set_min_from_curdata ( void );
void 	set_max_from_curdata ( void );
void	beep		     ( void );
void    invalidate_all_saveframes( void );
void	view_set_XY_plot_axis( char * );
void	view_plot_XY_fmt_x_val( float val, int dimindex, char *s, size_t slen );
void 	view_change_dat	     ( size_t index, float new_val );
void	view_get_scaled_size ( int blowup, size_t old_nx, size_t old_ny, size_t *new_nx, size_t *new_ny );
void 	view_change_transform( int delta );
void 	view_recompute_colorbar( void );
void    view_set_range_frame ( void );
void    view_set_range       ( void );
void    view_set_scan_dims   ( void );
void 	view_data_edit       ( void );
void 	view_information     ( void );
long 	view_current_nt      ( void );

/******************************************************************************
 * in overlay.c
 */
void 	do_overlay		( int n, char *custom_filename, int suppress_screen_changes );
char 	**overlay_names		( void );
int 	overlay_current		( void );
int 	overlay_n_overlays	( void );
void 	determine_overlay_base_dir( char *overlay_base_dir, int n );
int 	overlay_custom_n	( void );
void	overlay_init		( void );

/******************************************************************************
 * in udu.c
 */
void 	udu_utinit( char *path );
int 	udu_utistime( char *dimname, char *units );
int 	udu_calc_tgran( int fileid, NCVar *v, int dimid );
void 	udu_fmt_time( char *temp_string, size_t temp_string_len, double new_dimval, NCDim *dim, int include_granularity );

/******************************************************************************
 * in epic_time.c
 */
void epic_fmt_time( char *temp_string, size_t temp_string_len, double new_dimval, NCDim *dim );
int  epic_istime0( int fileid, NCVar *v, NCDim *d );
int  epic_calc_tgran( int fileid, NCDim *d );

/******************************************************************************
 * in do_print.c
 */
void 	print_init	( void );
void 	do_print	( void );

/******************************************************************************
 * in handle_rc_file.c
 */
int 	write_state_to_file( Stringlist *state_to_save );
int 	read_state_from_file( Stringlist **state );
Stringlist *get_persistent_state();

/******************************************************************************
 * in interface_glue.c -- toolkit-agnostic logic factored out of upstream's
 * src/interface/interface.c because core itself calls these (not just the
 * UI); see that file's header comment.
 */
void	in_variable_selected	( char *var_name );
void	in_colormap_selected	( char *name );
void	in_button_pressed	( int button_id, int modifier );
void	in_error		( char *message );

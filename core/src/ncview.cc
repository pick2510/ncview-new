/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 2026 Dominik Strebel
 * Copyright (C) 1993 through 2024 David W. Pierce
 *
 * This program  is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 3, as 
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

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/frame_cache.h"
#include "ncview/protos.h"

/* These hold data for our colormaps */

/* A. Shchepetkin: new colormaps are added here */
#include "ncview/colormaps_bright.h"
#include "ncview/colormaps_banded.h"
#include "ncview/colormaps_rainbow.h"
#include "ncview/colormaps_jaisnb.h"
#include "ncview/colormaps_jaisnc.h"
#include "ncview/colormaps_jaisnd.h"
#include "ncview/colormaps_blu_red.h"
#include "ncview/colormaps_manga.h"
#include "ncview/colormaps_jet.h"
#include "ncview/colormaps_wheel.h"

/* Post-port addition: matplotlib's perceptually-uniform colormap family,
 * added here alongside the other contributed colormaps rather than with
 * the M0-ported originals below. */
#include "ncview/colormaps_viridis.h"
#include "ncview/colormaps_plasma.h"
#include "ncview/colormaps_inferno.h"
#include "ncview/colormaps_magma.h"
#include "ncview/colormaps_cividis.h"

/* the following are original colormaps from ncview */
#include "ncview/colormaps_3gauss.h"
#include "ncview/colormaps_3saw.h"
#include "ncview/colormaps_bw.h"
#include "ncview/colormaps_default.h"
#include "ncview/colormaps_detail.h"
#include "ncview/colormaps_extrema.h"
#include "ncview/colormaps_helix.h"
#include "ncview/colormaps_helix2.h"
#include "ncview/colormaps_hotres.h"
#include "ncview/colormaps_ssec.h"

/* Program defaults in a easy-to-find place */
#define DEFAULT_INVERT_PHYSICAL	false
#define DEFAULT_INVERT_COLORS	false
#define DEFAULT_BLOWUP		1
#define DEFAULT_MIN_MAX_METHOD	MinMaxMethod::Fast
#define DEFAULT_N_COLORS	200
#define DEFAULT_PRIVATE_CMAP	false
#define DEFAULT_BLOWUP_TYPE	BlowupType::Bilinear
#define DEFAULT_SHRINK_METHOD	ShrinkMethod::Mean
#define DEFAULT_SAVEFRAMES	true
#define DEFAULT_NO_AUTOFLIP	false
#define DEFAULT_LISTSEL_MAX	40
#define DEFAULT_COLOR_BY_NDIMS	true
#define DEFAULT_AUTO_OVERLAY	true

AppContext g_app;
Options	  options( g_app.session );
Dataset   &g_dataset = g_app.session.dataset();
std::vector<std::unique_ptr<NCVar>> &variables = g_dataset.variablesMutable();
std::vector<ncv_pixel> &pixel_transform = g_app.session.pixelTransform();
FrameCache &framestore = g_app.session.frameCache();
Stringlist *read_in_state;

static void init_cmaps_from_data();
static void init_cmap_from_data( const char *colormap_name, int *data );
static int get_cmaps_from_dir( const char *dir_name );
static int ncview_cmap_suffix( const char *s, int *n_suffix );
static int any_var_in_group( const std::vector<std::unique_ptr<NCVar>> &vars );

/***********************************************************************************************/
	int
ncview_main( int argc, char **argv )
{
	Stringlist *input_files, *state_to_save;
	int	   err, found_state_file;

	/* Initialize misc constants */
	initialize_misc();

	/* Read in our state file from a previous run of ncview 
	 */
	read_in_state = NULL;	/* Note: a global var. Set to null to flag following routine to make a new stringlist */
	err = read_state_from_file( &read_in_state );
	if( err == 0 ) 	
		found_state_file = true;
	else
		found_state_file = false;

	in_parse_args               ( &argc, argv );
	input_files = parse_options ( argc,  argv );	/* This parses ALL the non-X11 command line options, not just the input files */

	/* No files given on the command line -- ask the user via a native
	 * file-open dialog instead of just erroring out below. This is what
	 * makes launching ncview with no arguments (double-click, dock icon,
	 * "Open with") usable instead of a dead end.
	 */
	if( stringlist_len( input_files ) == 0 )
		input_files = in_choose_input_files();

	if( stringlist_len( input_files ) == 0 ) {
		fprintf( stderr, "ncview: no input files given; exiting.\n" );
		exit( 0 );
		}

	determine_file_type         ( input_files );

	options.blowup       = 1;

	/* this routine sets up the 'variables' structure */
	initialize_file_interface   ( input_files );

	if( n_vars_in_list( variables ) == 0 ) {
		fprintf( stderr, "no displayable variables found!\n" );
		exit( -1 );
		}

	/* If any vars are in groups, we build the interface differently. 
	 * I pass this information through the global "options" struct.
	 */
	if( any_var_in_group( variables )) {
		options.enable_group_sel = true;
		options.varsel_style = VarselStyle::Menu;
		}
	else
		options.enable_group_sel = false;

	/* This initializes the colormaps, and then the X widows system */
	if( options.debug ) printf( "Initializing display interface...\n" );
	initialize_display_interface(); 
	if( options.debug ) printf( "Initializing printing subsystem...\n" );
	print_init();
	if( options.debug ) printf( "Initializing overlays...\n" );
	overlay_init();

	/* If there is only one variable, make it the active one */
	if( n_vars_in_list( variables ) == 1 ) {
		/* set_scan_variable     ( variables       ); */
		in_indicate_active_var( const_cast<char *>(variables[0]->name.c_str()) );
		}

	/* If we didn't find a state file (".ncviewrc") when we started up, then
	 * write a new one out now that we are all initialized
	 */
	if( found_state_file == false ) {
		state_to_save = get_persistent_state();
		if( (err = write_state_to_file( state_to_save )) != 0 ) {
			fprintf( stderr, "Error %d while trying to save options file \"$HOME/.ncviewrc\".\n", err );
			}
		stringlist_delete_entire_list( state_to_save );
		}

	process_user_input();

	return(0);
}

/***********************************************************************************************/
	Stringlist *
parse_options( int argc, char *argv[] )
{
	int	i, n;
	Stringlist *file_list = NULL;
	char	bufr[1000], *comma_ptr;

	for( i=1; i<argc; i++ ) {
		if( argv[i][0] == '-' ) {
			/* found an entry that is in option syntax */
			if( strncmp( argv[i], "-min", 4 ) == 0 ) {

				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -minmax argument must be followed by one of these: fast med slow all\n" );
					exit(-1);
					}

				if( strncmp( argv[i+1], "fast", 4 ) == 0 ) {
					options.min_max_method  = MinMaxMethod::Fast;
					i++;
					}
				else if( strncmp( argv[i+1], "med", 3 ) == 0 ) {
					options.min_max_method  = MinMaxMethod::Med;
					i++;
					}
				else if( strncmp( argv[i+1], "slow", 4 ) == 0 ) {
					options.min_max_method  = MinMaxMethod::Slow;
					i++;
					}
				else if( strncmp( argv[i+1], "exh", 3 ) == 0 ) {
					options.min_max_method  = MinMaxMethod::Exhaust;
					i++;
					}
				else if( strncmp( argv[i+1], "all", 3 ) == 0 ) {
					options.min_max_method  = MinMaxMethod::Exhaust;
					i++;
					}
				else
					{
					fprintf( stderr, "unrecognizied option: %s %s\n",
						argv[i], argv[i+1] );
					/* doesn't return */
					useage();
					}
				}

			else if( strncmp( argv[i], "-cal", 4 ) == 0 ) {
				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -cal must be followed by a calendar name\n" );
					exit(-1);
					}
				options.calendar = argv[i+1];
				i++;
				}

			else if( strncmp( argv[i], "-w", 2 ) == 0 ) {
				print_no_warranty();
				exit( 0 );
				}

			else if( strncmp( argv[i], "-pri", 4 ) == 0 )
				options.private_colormap = true;

			else if( strncmp( argv[i], "-deb", 4 ) == 0 )
				options.debug = true;

			else if( strncmp( argv[i], "-beep", 5 ) == 0 )
				options.beep_on_restart = true;

			else if( strncmp( argv[i], "-pause_on_restart", 17 ) == 0 )
				options.stop_on_restart = true;

			else if( strncmp( argv[i], "-fra", 4 ) == 0 )
				options.dump_frames = true;

			else if( strncmp( argv[i], "-small", 6 ) == 0 )
				options.small = true;

			else if( strncmp( argv[i], "-ext", 4 ) == 0 )
				options.want_extra_info = true;

			else if( strncmp( argv[i], "-mti", 3 ) == 0 )
				/* -mtitle's argument was stored into options.window_title,
				 * a field that nothing ever read (removed as dead code) --
				 * this flag has been a documented no-op since at least the
				 * start of this port. Still consume its argument so later
				 * flags parse correctly. */
				i++;

			else if( strncmp( argv[i], "-noauto", 7 ) == 0 )
				options.no_autoflip = true;

			else if( strncmp( argv[i], "-no1d", 5 ) == 0 )
				options.no_1d_vars = true;

			else if( strncmp( argv[i], "-show_sel", 9 ) == 0 )
				options.show_sel = true;

			else if( strncmp( argv[i], "-no_char_dim", 12 ) == 0 )
				options.no_char_dims = true;

			else if( strncmp( argv[i], "-notconv", 8 ) == 0 )
				options.t_conv = false;

			else if( strncmp( argv[i], "-shrink_mode", 12) == 0 )
				options.shrink_method = ShrinkMethod::Mode;

			else if( strncmp( argv[i], "-repl", 5) == 0 )
				/* Pre-existing quirk, preserved: this sets options.blowup (the
				 * blowup magnitude), not options.blowup_type, even though the
				 * value 1 here is BlowupType::Replicate's numeric value -- see
				 * modernization.md's Phase 1 follow-up notes. */
				options.blowup = 1;

			else if( strncmp( argv[i], "-c", 2 ) == 0 ) {
				print_copying();
				exit( 0 );
				}

			else if( strncmp( argv[i], "-no_color_ndims", 7 ) == 0 ) {
				options.color_by_ndims = false;
				}

			else if( strncmp( argv[i], "-no_auto_overlay", 7 ) == 0 ) {
				options.auto_overlay = false;
				}

			else if( strncmp( argv[i], "-autoscale", 9 ) == 0 ) {
				options.autoscale = true;
				}

			else if( strncmp( argv[i], "-scale", 6 ) == 0 ) {
				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -scale must be followed by a number\n" );
					exit(-1);
					}
				sscanf( argv[i+1], "%f", &(options.scale) );
				i++;
				}

			else if( strncmp( argv[i], "-offset", 7 ) == 0 ) {
				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -offset must be followed by a number\n" );
					exit(-1);
					}
				sscanf( argv[i+1], "%f", &(options.offset) );
				i++;
				}

			else if( strncmp( argv[i], "-listsel_max", 7 ) == 0 ) {
				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -listsel_max must be followed by an integer\n" );
					exit(-1);
					}
				sscanf( argv[i+1], "%d", &(options.listsel_max) );
				i++;
				}

			else if( strncmp( argv[i], "-missvalrgb", 11 ) == 0 ) {
				if( i > (argc-4) ) {
					fprintf( stderr, "Error, -missvalrgb must be followed by three integers (r g b)\n" );
					exit(-1);
					}
				sscanf( argv[i+1], "%d", &(options.missval_r) );
				i++;
				sscanf( argv[i+1], "%d", &(options.missval_g) );
				i++;
				sscanf( argv[i+1], "%d", &(options.missval_b) );
				i++;
				}

			else if( strncmp( argv[i], "-nc", 3 ) == 0 ) {
				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -nc must be followed by an integer\n" );
					exit(-1);
					}
				sscanf( argv[i+1], "%d", &(options.n_colors) );
				/* Data colors are stored at pixel indices
				 * [n_extra_colors, n_extra_colors+n_colors) in a
				 * 256-entry (ncv_pixel is a byte) colormap table --
				 * n_colors can be at most 255-n_extra_colors, not
				 * 255, or util.cc's data_to_pixels() computes an
				 * out-of-range index for the brightest data values. */
				if( options.n_colors > (255 - options.n_extra_colors) ) {
					fprintf( stderr, "maximum number of colors is currently %d\n",
						255 - options.n_extra_colors );
					exit( -1 );
					}
				i++;
				}

			else if( strncmp( argv[i], "-max", 4 ) == 0 ) {

				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -maxsize argument must be followed by either a single integer (pct of screen) or two comma separated integers (max width,max height)\n" );
					exit(-1);
					}
				/* See if there is a comma in the following arg */
				i++;
				if( (comma_ptr = strstr( argv[i], "," )) == NULL ) {
					if( (sscanf( argv[i], "%d", &(options.maxsize_pct) ) != 1 ) ||
					    (options.maxsize_pct < 30) ||
					    (options.maxsize_pct > 100)) {
					    	fprintf( stderr, "Error, when the -maxsize arg is followed by a single number, it must be an integer between 30 and 100\n" );
						exit(-1);
						}
					}
				else
					{
					/* parse a width,height pair of ints separated by a comma */
					options.maxsize_pct = -1;	/* flag using width,height rather than pct */
					if( strlen(argv[i]) > 900 ) {
						fprintf( stderr, "Error, string specified in -maxsize too long!\n" );
						exit(-1);
						}
					n = comma_ptr - argv[i];
					strncpy( bufr, argv[i], n );
					bufr[n] = '\0';
					if( (sscanf( bufr, "%d", &(options.maxsize_width) ) != 1 ) ||
					    (options.maxsize_width < 30) ||
					    (options.maxsize_width > 99999)) {
					    	fprintf( stderr, "Error, specified -maxsize width must be an integer between 30 and 100\n" );
						exit(-1);
						}
					snprintf( bufr, sizeof(bufr), "%s", argv[i]+n+1 );
					if( (sscanf( bufr, "%d", &(options.maxsize_height) ) != 1 ) ||
					    (options.maxsize_height < 30) ||
					    (options.maxsize_height > 99999)) {
					    	fprintf( stderr, "Error, specified -maxsize height must be an integer between 30 and 100\n" );
						exit(-1);
						}
					}
				}

			else	/* put other options here */
				{
				fprintf( stderr, "unrecognizied option: %s\n", argv[i] );
				/* doesn't return */
				useage();
				}
			}
		else /* found an entry which is NOT in option syntax -- assume a filename */
			stringlist_add_string( &file_list, argv[i] );
		} /* end of i loop through argv's */

	return( file_list );
}

/***********************************************************************************************/
/* Every options field and framestore default this port sets before first use,
 * split out from initialize_misc() below so it can be re-run on its own
 * (see tests/support/session_fixture.h) without repeating
 * udu_utinit(NULL) -- test_udunits_helper.h documents why calling that a
 * second time silently breaks every already-cached ut_unit comparison.
 * Idempotent and side-effect-free otherwise (no I/O, no process-global
 * state besides options/framestore themselves), so safe to call as often
 * as needed.
 */
	void
reset_session_defaults()
{
	options.invert_physical  = DEFAULT_INVERT_PHYSICAL;
	options.invert_colors    = DEFAULT_INVERT_COLORS;
	options.blowup           = DEFAULT_BLOWUP;
	options.shrink_method    = DEFAULT_SHRINK_METHOD;
	options.min_max_method   = DEFAULT_MIN_MAX_METHOD;
	options.transform        = Transform::None;
	options.n_colors 	 = DEFAULT_N_COLORS;
	options.n_extra_colors 	 = 10;
	options.private_colormap = DEFAULT_PRIVATE_CMAP;
	options.debug		 = false;
	options.show_sel	 = false;
	options.want_extra_info  = false;
	options.beep_on_restart  = false;
	options.stop_on_restart  = false;
	options.small  		 = false;
	options.blowup_type      = DEFAULT_BLOWUP_TYPE;
	options.save_frames      = DEFAULT_SAVEFRAMES;
	options.no_autoflip      = DEFAULT_NO_AUTOFLIP;
	options.t_conv      	 = true;
	options.varsel_style	 = VarselStyle::List;
	options.dump_frames	 = false;
	options.listsel_max	 = DEFAULT_LISTSEL_MAX;
	options.color_by_ndims	 = DEFAULT_COLOR_BY_NDIMS;
	options.auto_overlay	 = DEFAULT_AUTO_OVERLAY;
	options.autoscale	 = false;
	options.scale		 = 1.e30;	/* This val means do NOT do any user scaling of data */
	options.offset		 = 1.e30;	/* This val means do NOT do any user offset of data */

	options.overlay          = std::make_unique<OverlayOptions>();
	options.overlay->doit    = false;

	options.maxsize_pct	 = 75;	/* maximum size of a window, in percent of screen, before switching to scrollbars */

	/* Set default color to use for missing data */
	options.missval_r 	= 255;
	options.missval_g 	= 255;
	options.missval_b 	= 255;

	framestore = FrameCache();

}

/***********************************************************************************************/
	void
initialize_misc()
{
	print_disclaimer();

	udu_utinit( NULL );
	reset_session_defaults();
}

/***********************************************************************************************/
	void
initialize_colormaps()
{
	char	*ncview_base_dir;

	/* ncview has a useful set of built-in colormaps.  The set of built-in
	 * colormaps can be augmented by user-specified colormaps that are contained
	 * in simple ASCII files with 256 lines, where each line has 3 entries
	 * (separated by spaces), which indicate the R, B, and G values.  Each
	 * value must be an integer between 0 and 255, inclusive.  User-specified
	 * colormaps are contained in files with the extension of ".ncmap", 
	 * and can live in the following places:
	 *  1) NCVIEW_LIB_DIR, which is determined at installation time.
	 *     	A reasonable choice is "/usr/local/lib/ncview".
	 *  2) In a directory named by the environmental variable
	 *	"NCVIEWBASE".
	 *  3) If there is no environmental variable "NCVIEWBASE", then
	 *     	in $HOME.
	 *  4) In the current working directory.
	 */

	/* Get built-in colormaps */
	init_cmaps_from_data();

	/* Get user-specified colormaps, if any */
#ifdef NCVIEW_LIB_DIR
	get_cmaps_from_dir( NCVIEW_LIB_DIR );
#endif
	ncview_base_dir = (char *)getenv( "NCVIEWBASE" );
	if( ncview_base_dir == NULL )
		ncview_base_dir = (char *)getenv( "HOME" );

	if( ncview_base_dir != NULL )
		get_cmaps_from_dir( ncview_base_dir );

	get_cmaps_from_dir( "." );
}

/***********************************************************************************************/
/* Returns 0 if the passed char string "s" has a valid ncview cmap file suffix, and 1 otherwise.
 * If a 0 is returned, then n_suffix is also set to the suffix length, including the period.
 */
	int
ncview_cmap_suffix( const char *s, int *n_suffix )
{
	int		nc;

	if( s == NULL ) 
		return( 1 );

	nc = strlen( s );
	if( nc < 5 ) 
		return( 1 );

	if( strncasecmp( (s+nc-4), ".ncm", 4 ) == 0 ) {
		*n_suffix = 4;
		return( 0 );
		}

	if( nc < 7 ) 
		return( 1 );

	if( strncasecmp( (s+nc-6), ".ncmap", 6 ) == 0 ) {
		*n_suffix = 6;
		return( 0 );
		}

	return( 1 );
}

/***********************************************************************************************/
	int
get_cmaps_from_dir( const char *dir_name )
{
	DIR		*ncdir = NULL;
	struct dirent	*dir_entry;
	int		n_colormaps = 0, n_suffix;

	if( options.debug ) 
		printf( "Getting colormaps from dir >%s<\n", dir_name );

	ncdir     = opendir( dir_name );
	if( ncdir == NULL ) 
		return( 0 );
	dir_entry = readdir( ncdir    );
	while( dir_entry != NULL ) {
		/* Additional allowed filenames as per suggestion by 
		 * Arlindo da Silva for his Windows port
		 */
		if( ncview_cmap_suffix( dir_entry->d_name, &n_suffix ) == 0) {
			init_cmap_from_file( dir_name, dir_entry->d_name, n_suffix );
			n_colormaps++;
			}
		dir_entry = readdir( ncdir );
		}
	closedir( ncdir );

	return( n_colormaps );
}

/***********************************************************************************************/
	void
init_cmaps_from_data()
{
/* the following are original colormaps from ncview */

	init_cmap_from_data( "3gauss",  cmap_3gauss  );
	init_cmap_from_data( "detail",  cmap_detail  );
	init_cmap_from_data( "ssec",    cmap_ssec    );

/* A. Shchepetkin: new colormaps are added here */

        init_cmap_from_data( "bright",  cmap_bright  );
        init_cmap_from_data( "banded",  cmap_banded  );
        init_cmap_from_data( "rainbow", cmap_rainbow );
        init_cmap_from_data( "jaisnb",  cmap_jaisnb  );
        init_cmap_from_data( "jaisnc",  cmap_jaisnc  );
        init_cmap_from_data( "jaisnd",  cmap_jaisnd  );
        init_cmap_from_data( "blu_red", cmap_blu_red );
        init_cmap_from_data( "manga",   cmap_manga   );
        init_cmap_from_data( "jet",     cmap_jet     );
        init_cmap_from_data( "wheel",   cmap_wheel   );
        init_cmap_from_data( "viridis", cmap_viridis );
        init_cmap_from_data( "plasma",  cmap_plasma  );
        init_cmap_from_data( "inferno", cmap_inferno );
        init_cmap_from_data( "magma",   cmap_magma   );
        init_cmap_from_data( "cividis", cmap_cividis );

/* the following are the rest of the original colormaps from ncview */

	init_cmap_from_data( "3saw",    cmap_3saw    );
	init_cmap_from_data( "bw",      cmap_bw      );
	init_cmap_from_data( "default", cmap_default );
	init_cmap_from_data( "extrema", cmap_extrema );
	init_cmap_from_data( "helix",   cmap_helix   );
	init_cmap_from_data( "helix2",  cmap_helix2  );
	init_cmap_from_data( "hotres",  cmap_hotres  );
}

/***********************************************************************************************/
	void
init_cmap_from_data( const char *colormap_name, int *data )
{
	int	i;
	unsigned char r[256], g[256], b[256];

	if( options.debug ) 
		printf( "    ... initting cmap >%s< from supplied data\n", colormap_name );

	for( i=0; i<256; i++ ) {
		r[i] = (unsigned char)data[i*3+0];
		g[i] = (unsigned char)data[i*3+1];
		b[i] = (unsigned char)data[i*3+2];
		}

	in_create_colormap( colormap_name, r, g, b );
}

/***********************************************************************************************/
	void
init_cmap_from_file( const char *dir_name, const char *file_name, int n_suffix )
{
	char 	*colormap_name;
	FILE	*cmap_file;
	int	i, nentries, r_entry, g_entry, b_entry;
	char	line[ 128 ], *long_file_name;
	unsigned char r[256], g[256], b[256];
	size_t	slen;

	if( options.debug )
		printf( "    ... initting cmap >%s<\n", file_name );

	/* Colormap name is the file name without the '.ncmap' or '.ncm' extension */
	std::vector<char> colormap_name_buf( strlen(file_name)-(n_suffix-1) );
	colormap_name = colormap_name_buf.data();
	/* colormap_name_buf is sized to exactly fit this copy plus the NUL
	 * written just below, so this can't truncate; memcpy (rather than
	 * strncpy) avoids -Wstringop-truncation's "bound depends on the
	 * length of the source" heuristic, which can't see that. */
	memcpy( colormap_name, file_name, strlen(file_name)-n_suffix );
	*(colormap_name + strlen(file_name)-n_suffix) = '\0';

	/* Make sure this colormap name isn't already known */
	if( x_seen_colormap_name( colormap_name )) {
		if( options.debug )
			printf( "Already have a colormap named >%s<, not NOT inittig colormap from file >%s<\n",
				colormap_name, file_name);
		return;
		}

	/* Read in the r, g, b values */
	slen = strlen(file_name) + strlen(dir_name) + 5;  /* add space for intermediate slash and trailing NULL */
	std::vector<char> long_file_name_buf( slen );  /* add space for intermediate slash and trailing NULL */
	long_file_name = long_file_name_buf.data();
	snprintf( long_file_name, slen, "%s/%s", dir_name, file_name );
	if( (cmap_file = fopen( long_file_name, "r" )) == NULL ) {
		fprintf( stderr, "ncview.c: init_cmap_from_file: error " );
		fprintf( stderr, "opening file %s\n", long_file_name );
		return;
		}
	for( i=0; i<256; i++ ) {
		if( fgets( line, 128, cmap_file ) == NULL ) {
			fprintf( stderr, "ncview: init_cmap_from_file: file %s finished ",
					long_file_name );
			fprintf( stderr, "on line %d, but should have 256 lines: not a valid ncview cmap file\n", i+1 );
			return;
			}

		nentries = sscanf( line, "%d %d %d", &r_entry, &g_entry, &b_entry );
		if( nentries != 3 ) {
			fprintf( stderr, "ncview: init_cmap_from_file: incorrect number " );
			fprintf( stderr, "of entries on the line.  Should be 3\n" );
			fprintf( stderr, "file %s, line %d\n", long_file_name, i+1 );
			return;
			}

		if( check( r_entry, 0, 255 ) < 0 ) {
			fprintf( stderr, "ncview: init_cmap_from_file: first entry (red) " );
			fprintf( stderr, "is outside valid limits of 0 to 255.\n" );
			fprintf( stderr, "file %s, line %d\n", long_file_name, i+1 );
			return;
			}

		if( check( g_entry, 0, 255 ) < 0 ) {
			fprintf( stderr, "ncview: init_cmap_from_file: second entry (green) " );
			fprintf( stderr, "is outside valid limits of 0 to 255.\n" );
			fprintf( stderr, "file %s, line %d\n", long_file_name, i+1 );
			return;
			}

		if( check( b_entry, 0, 255 ) < 0 ) {
			fprintf( stderr, "ncview: init_cmap_from_file: third entry (blue) " );
			fprintf( stderr, "is outside valid limits of 0 to 255.\n" );
			fprintf( stderr, "file %s, line %d\n", long_file_name, i+1 );
			return;
			}
		r[i] = (unsigned char)r_entry;
		g[i] = (unsigned char)g_entry;
		b[i] = (unsigned char)b_entry;
		}

	in_create_colormap( colormap_name, r, g, b );
}

/***********************************************************************************************/
	void
initialize_file_interface( Stringlist *input_files )
{
	int	idim, nvars;

	if( options.debug )
		printf( "Initializing file interface...\n" );

	if( input_files != NULL )
		for( auto &f : *input_files )
			fi_initialize( (char *)f.string.c_str() );
	if( options.debug )
		printf( "...calculating dim min & maxes...\n" );
	g_dataset.calcDimMinmaxes();

	/* Get the effective dimensionality of all the vars.
	 * Can't do this before we have read in all of the
	 * input file.
	 */
	nvars = 0;
	for( auto &var_owner : variables ) {
		NCVar *var = var_owner.get();
		nvars++;
		var->effective_dimensionality = 0;
		for( idim=0; idim<var->n_dims; idim++ ) {
			if( var->size[idim] > 1 )
				var->effective_dimensionality++;
			if( options.debug ) {
				/* var->dim[idim] is null for a non-scannable
				 * (singleton) dimension -- see util.cc's
				 * fill_dim_structs(), "Indicate non-scannable
				 * dimensions by a null entry". A pre-existing
				 * upstream bug unconditionally dereferenced it
				 * here too. */
				if( var->dim[idim] != nullptr )
					printf( "var %s has %d dims, dim %d: >%s< len %zu\n",
						var->name.c_str(), var->n_dims, idim,
						var->dim[idim]->name.c_str(), var->dim[idim]->size );
				else
					printf( "var %s has %d dims, dim %d: (non-scannable, no dim struct)\n",
						var->name.c_str(), var->n_dims, idim );
				}
			}
		if( options.debug ) {
			printf( "variable %s had effective_dimensionality of %d\n",
				var->name.c_str(), var->effective_dimensionality );
			}
		}

	/* Now that we have read in all the files, we can
	 * gather any scalar coordinate information (which
	 * might possibly change in each file)
	 */
	g_dataset.cacheScalarCoordInfo();

	if( nvars > options.listsel_max )
		options.varsel_style = VarselStyle::Menu;

	if( options.debug ) 
		printf( "Done initializing file interface...\n" );
}

/***********************************************************************************************/
	void
initialize_display_interface()
{
	/* Upstream allocated and filled this identity/remap table in
	 * interface/colormap_funcs.c's x_create_colormap() -- the X11
	 * colorcell-allocation file this port intentionally doesn't carry over
	 * (FLTK expands a pixel index straight to RGB, the same as upstream's
	 * own TrueColor branch there: pixel_transform[i] = i). Without this,
	 * util.cc:data_to_pixels() unconditionally dereferences a NULL
	 * pixel_transform for the first missing/fill-value pixel it sees.
	 */
	pixel_transform.resize( options.n_colors+options.n_extra_colors );
	for( int i=0; i<options.n_colors+options.n_extra_colors; i++ )
		pixel_transform[i] = (ncv_pixel)i;

	initialize_colormaps();

	/* Make the colormaps in the program congruent in order
	 * and "enabled-ness" with the read-in state
	 */
	x_check_legal_colormap_loaded();

	if( options.debug ) printf( "...initializing X interface\n" );
	in_initialize();
	if( options.debug ) printf( "...done with initializing X interface\n" );
}

/***********************************************************************************************/
	void
process_user_input()
{
	/* This call never returns, as it loops, handling user interface events */
	in_process_user_input();
}

/***********************************************************************************************/
/* Only correct exit point for 'ncview' */
	void
quit_app()
{
	exit( 0 );
}

/***********************************************************************************************/
	int
check( int val, int min, int max )
{
	if( (val >= min) && (val <= max) )
		return( 0 );
	else
		return( -1 );
}

/***********************************************************************************************/
/* Make a quickie colormap in case none is found */
	void
create_default_colormap()
{
	ncv_pixel	r[256], g[256], b[256];
	int		i;

	for( i=0; i<256; i++ ) {
		r[i] = i;
		g[i] = 255 - abs(i-128);
		b[i] = 255-i;
		}

	in_create_colormap( "default", r, g, b );
}

/***********************************************************************************************/
int any_var_in_group( const std::vector<std::unique_ptr<NCVar>> &vars ) {

	for( const auto &cursor : vars )
		if( count_nslashes( cursor->name.c_str() ) > 0 )
			return( 1 );

	return( 0 );
}

/***********************************************************************************************/
	void
print_disclaimer()
{
fprintf( stderr, "%s\n", PROGRAM_ID );
fprintf( stderr, "https://cirrus.ucsd.edu/ncview/\n" );
fprintf( stderr, "Copyright (C) 1993 through 2024, David W. Pierce\n" );
fprintf( stderr, "This C++/FLTK port, Copyright (C) 2026 Dominik Strebel\n" );
fprintf( stderr, "Ncview comes with ABSOLUTELY NO WARRANTY; for details type `ncview -w'.\n" );
fprintf( stderr, "This is free software licensed under the Gnu General Public License version 3; type `ncview -c' for redistribution details.\n\n" );
}

/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 1993 through 2024 David W. Pierce
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

/*     define DEBUG */

#include <array>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

#define PAGE_WIDTH_INCHES	8.5
#define PAGE_HEIGHT_INCHES	11.5
#define PAGE_X_MARGIN		1.5	/* Inches */
#define PAGE_UPPER_Y_MARGIN	2.0	/* Inches */
#define PAGE_LOWER_Y_MARGIN	4.0	/* Inches */

#define FONT_SIZE		11
#define HEADER_FONT_SIZE	16
#define FONT_NAME		"Helvetica"
#define LEADING			3

#define DEFAULT_DEVICE		Device::Printer
#define	INCLUDE_OUTLINE		true
#define	INCLUDE_TITLE		true
#define	INCLUDE_AXIS_LABELS	true
#define	INCLUDE_EXTRA_INFO	true
#define	INCLUDE_ID		true
#define TEST_ONLY		false

#define	ID_FONT_SIZE_SCALE	0.7	/* How much smaller ID font size is than regular */

extern View 	*view;
extern Options 	options;

static PrintOptions printopts;

/* getlogin() is POSIX-only -- not provided by MinGW-w64's Windows CRT --
 * used only to stamp a username on the "include ID" printout footer, so a
 * portable fallback via the USERNAME environment variable (always set on
 * Windows) is good enough; never fails outright, so callers always get a
 * usable string.
 */
static char *get_login_name( void )
{
#ifdef _WIN32
	char *name = getenv( "USERNAME" );
#else
	char *name = getlogin();
#endif
	return name ? name : (char *)"unknown";
}

static void print_header( FILE *out_file, float scale, size_t x, size_t y, size_t top_of_image );
static void calc_scale( float *scale, size_t x, size_t y );
static void set_font( FILE *outf, const char *name, int size );
static void do_outline( FILE *f, size_t x, size_t y );
static void print_other_info( FILE *out_file, float output_scale, size_t x_size, size_t y_size, 
			size_t center_x, size_t center_y, size_t top_of_image, size_t bot_of_image );

/********************************************************************/

	void
print_init( void )
{
	printopts.page_width    	= PAGE_WIDTH_INCHES; 
	printopts.page_height   	= PAGE_HEIGHT_INCHES;
	printopts.page_x_margin 	= PAGE_X_MARGIN;
	printopts.page_upper_y_margin 	= PAGE_UPPER_Y_MARGIN;
	printopts.page_lower_y_margin 	= PAGE_LOWER_Y_MARGIN;
	printopts.ppi			= 72.0;

	printopts.leading 		= LEADING;
	printopts.font_size		= FONT_SIZE;
	printopts.header_font_size 	= HEADER_FONT_SIZE;

	printopts.output_device		= DEFAULT_DEVICE;
	printopts.include_outline	= INCLUDE_OUTLINE;
	printopts.include_id		= INCLUDE_ID;
	printopts.include_title		= INCLUDE_TITLE;
	printopts.include_axis_labels	= INCLUDE_AXIS_LABELS;
	printopts.include_extra_info	= INCLUDE_EXTRA_INFO;

	printopts.test_only		= TEST_ONLY;

	printopts.font_name = FONT_NAME;

	printer_options_init(); /* this initializes the X-windows interface
				   part of the pinter options panel */
}

/*************************************************************************/

	void
do_print( void )
{
	long	i, j;
	size_t	x_size, y_size, scaled_x_size, scaled_y_size, top_of_image, bot_of_image, 
		center_x, center_y, left_of_image, right_of_image;
	char	outfname[1024], tstr[1500];
	int     outfid;
	FILE	*outf;
	float	output_scale;
	int	r, g, b, n_print;
	ncv_pixel pix;

#ifdef DEBUG
	fprintf( stderr, "entering do_print()\n" );
#endif
	x_size = view->variable->size[view->x_axis_id];
	y_size = view->variable->size[view->y_axis_id];
	view_get_scaled_size( options.blowup, x_size, y_size, &scaled_x_size, &scaled_y_size );

	printopts.out_file_name = "ncview." + view->variable->name + ".ps";
	if( printer_options( &printopts ) == Message::Cancel )
		return;

	if( printopts.output_device == Device::Printer ) {
	    printopts.out_file_name = "/tmp/ncview.XXXXXX";
	    outfid = mkstemp( printopts.out_file_name.data() );
	    if (outfid == -1) {
		snprintf( tstr, 1499, "Error opening temporary file for output!\n" );
		in_error( tstr );
		return;
	    }
	    if( (outf = fopen(printopts.out_file_name.c_str(), "a" )) == NULL ) {
		snprintf( tstr, 1499, "Error opening file %s for output!\n",
			 printopts.out_file_name.c_str() );
		in_error( tstr );
		return;
	    }
	    close(outfid);
	}
	else {
	    if( warn_if_file_exits( const_cast<char *>(printopts.out_file_name.c_str()) ) == Message::Cancel )
		return;

	    if( (outf = fopen(printopts.out_file_name.c_str(), "w" )) == NULL ) {
		snprintf( tstr, 1499, "Error opening file %s for output!\n",
			 outfname );
		in_error( tstr );
		return;
	    }
	}
	
	in_set_cursor_busy();
	calc_scale( &output_scale, scaled_x_size, scaled_y_size );

	/* These are all in absolute points in the default coordinate system */
	top_of_image   = (size_t)(printopts.page_height*printopts.ppi -
				printopts.page_upper_y_margin*printopts.ppi);
	bot_of_image   = top_of_image - (long)((float)scaled_y_size*output_scale);
	left_of_image  = (size_t)(printopts.page_x_margin*printopts.ppi);
	right_of_image = left_of_image + (long)((float)scaled_x_size*output_scale);
	center_y       = (top_of_image + bot_of_image)/2;
	center_x       = (size_t)(((float)left_of_image + (float)right_of_image)/2.0);

	print_header( outf, output_scale, scaled_x_size, scaled_y_size, top_of_image );

	/***** dump out the color image *****/
	if( ! printopts.test_only ) {
		view_draw( false, false ); /* Don't allow saveframes -- force reload of image data */
		n_print = 0;
		for( j=0; j<scaled_y_size; j++ ) {
			for( i=0; i<scaled_x_size; i++ ) {
				pix = view->pixels[j*scaled_x_size + i];
				pix_to_rgb( pix, &r, &g, &b );
				fprintf( outf, "%02x%02x%02x", (r>>8), (g>>8), (b>>8)); 
				n_print += 6;
				if( n_print > 70 ) {
					fprintf( outf, "\n" );
					n_print = 0;
					}
				}
			}
		fprintf( outf, "\n\n" );
		}
	
	/* Outline the color contour with lines */
	if( printopts.include_outline ) 
		do_outline( outf, scaled_x_size, scaled_y_size );

	fprintf( outf, "\n\ngrestore\n" );

	print_other_info( outf, output_scale, scaled_x_size, scaled_y_size, center_x, center_y, 
			top_of_image, bot_of_image );

#ifdef DEBUG
	fprintf( stderr, "exiting do_print()\n" );
#endif
}

/*************************************************************************/

	static void
print_other_info( FILE *outf, float output_scale, size_t x_size, size_t y_size, 
			size_t center_x, size_t center_y, 
			size_t top_of_image, size_t bot_of_image )
{
	char 	*x_dim_name, *y_dim_name,
		tstr[1500], tstr2[1000], *dim_name;
	std::string units, x_dim_longname, y_dim_longname, x_units, y_units,
		main_long_name, main_units, dim_longname, file_title;
	FDBlist	*fdb;
	NCDim	*d;
	int	i, type, has_bounds;
	time_t	sec_since_1970;
	double	temp_double, bound_min, bound_max;
	FILE	*f_dummy;

#ifdef DEBUG
	fprintf( stderr, "print_other_info: entering\n" );
#endif
	x_dim_name     = const_cast<char *>(view->variable->dim[view->x_axis_id]->name.c_str());
	x_dim_longname = fi_dim_longname( view->variable->files.front().get()->id, x_dim_name );
	x_units        = fi_dim_units( view->variable->files.front().get()->id, x_dim_name );

	y_dim_name     = const_cast<char *>(view->variable->dim[view->y_axis_id]->name.c_str());
	y_dim_longname = fi_dim_longname( view->variable->files.front().get()->id, y_dim_name );
	y_units        = fi_dim_units( view->variable->files.front().get()->id, y_dim_name );

	main_long_name = fi_long_var_name( view->variable->files.front().get()->id,
			view->variable->name );
	if( main_long_name.empty() )
		main_long_name = view->variable->name;
	main_units     = fi_var_units( view->variable->files.front().get()->id, view->variable->name );

	/***** Main variable name and units ******/
	if( printopts.include_title ) {
		snprintf( tstr, 1499, "%s", main_long_name.c_str() );
		if( !main_units.empty() ) {
			strncat( tstr, " (", sizeof(tstr) - strlen(tstr) - 1 );
			strncat( tstr, main_units.c_str(), sizeof(tstr) - strlen(tstr) - 1 );
			strncat( tstr, ")", sizeof(tstr) - strlen(tstr) - 1 );
			}

		/* move to the center, then half the string's width */
		set_font( outf, printopts.font_name.c_str(), printopts.header_font_size );
		fprintf( outf, "%ld %ld moveto\n",
				center_x,
				top_of_image+printopts.font_size );
		fprintf( outf, "(%s) stringwidth pop -0.5 mul 0 rmoveto\n", tstr );
		fprintf( outf, "(%s) show\n", tstr );
		}

	/***** X axis title *****/
	if( printopts.include_axis_labels ) {
		set_font( outf, printopts.font_name.c_str(), printopts.font_size );
		snprintf( tstr, sizeof(tstr), "%s", x_dim_longname.c_str() );
		if( !x_units.empty() ) {
			strncat( tstr, " (", sizeof(tstr) - strlen(tstr) - 1 );
			strncat( tstr, x_units.c_str(), sizeof(tstr) - strlen(tstr) - 1 );
			strncat( tstr, ")", sizeof(tstr) - strlen(tstr) - 1 );
			}
		fprintf( outf, "%ld %ld moveto\n",
			center_x, bot_of_image-(long)(1.5*(float)printopts.font_size) );
		fprintf( outf, "(%s) stringwidth pop -0.5 mul 0 rmoveto\n", tstr );
		fprintf( outf, "(%s) show\n", tstr );

		/***** Y axis title *****/
		set_font( outf, printopts.font_name.c_str(), printopts.font_size );
		snprintf( tstr, sizeof(tstr), "%s", y_dim_longname.c_str() );
		if( !y_units.empty() ) {
			strncat( tstr, " (", sizeof(tstr) - strlen(tstr) - 1 );
			strncat( tstr, y_units.c_str(), sizeof(tstr) - strlen(tstr) - 1 );
			strncat( tstr, ")", sizeof(tstr) - strlen(tstr) - 1 );
			}
		fprintf( outf, "%ld %ld moveto\n",
			center_x - (long)((float)x_size*output_scale/2.0),
			center_y );
		fprintf( outf, "gsave 90 rotate 0 %d rmoveto\n",
				(int)((float)printopts.font_size*output_scale) );
		fprintf( outf, "(%s) stringwidth pop -0.5 mul 0 rmoveto\n", tstr );
		fprintf( outf, "(%s) show grestore\n", tstr );
		}

	/***************** Other information *******************/
	if( printopts.include_extra_info ) {
		set_font( outf, printopts.font_name.c_str(), printopts.font_size );
		fprintf( outf, "%ld %ld moveto\n", (long)(printopts.page_x_margin*printopts.ppi),
			bot_of_image - 4*printopts.font_size );

		/**** File title ***/
		file_title = fi_title( view->variable->files.front().get()->id );
		if( !file_title.empty() ) {
			fprintf( outf, "gsave (%s) show grestore\n",
				file_title.c_str() );
			fprintf( outf, "0 %d rmoveto\n",
				-(printopts.leading+printopts.font_size) );
			}

		/*** Range of data ***/
		snprintf( tstr, 1499, "Range of %s: %g to %g %s", main_long_name.c_str(),
			view->variable->user_min, view->variable->user_max, main_units.c_str() );
		fprintf( outf, "gsave (%s) show grestore\n", tstr );
		fprintf( outf, "0 %d rmoveto\n", -(printopts.leading+printopts.font_size) );

		/*** Range of X axis ***/
		d = view->variable->dim[view->x_axis_id].get();
		if( x_units.empty() )
			snprintf( tstr, 1499, "Range of %s: %g to %g",
				x_dim_longname.c_str(), d->min, d->max);
		else
			snprintf( tstr, 1499, "Range of %s: %g to %g %s",
				x_dim_longname.c_str(), d->min, d->max, x_units.c_str() );
		fprintf( outf, "gsave (%s) show grestore\n", tstr );
		fprintf( outf, "0 %d rmoveto\n", -(printopts.leading+printopts.font_size) );

		/*** Range of Y axis ***/
		d = view->variable->dim[view->y_axis_id].get();
		if( options.invert_physical )
			snprintf( tstr, 1499, "Range of %s: %g to %g",
				y_dim_longname.c_str(), d->max, d->min );
		else
			snprintf( tstr, 1499, "Range of %s: %g to %g",
				y_dim_longname.c_str(), d->min, d->max );
		if( !y_units.empty() ) {
			strncat( tstr, " ", sizeof(tstr) - strlen(tstr) - 1 );
			strncat( tstr, y_units.c_str(), sizeof(tstr) - strlen(tstr) - 1 );
			}
		fprintf( outf, "gsave (%s) show grestore\n", tstr );
		fprintf( outf, "0 %d rmoveto\n", -(printopts.leading+printopts.font_size) );

		/*** Values of other dimensions ***/
		for(i=0; i<view->variable->n_dims; i++)
			if( (i != view->x_axis_id) &&
			    (i != view->y_axis_id) &&
			    (view->variable->dim[i].get() != NULL)) {
				dim_name     = const_cast<char *>(view->variable->dim[i]->name.c_str());
				dim_longname = fi_dim_longname( view->variable->files.front().get()->id, dim_name );
				units        = fi_dim_units( view->variable->files.front().get()->id, dim_name );
				type         = fi_dim_value( view->variable, i, view->var_place[i],
							&temp_double, tstr2, &has_bounds, &bound_min, &bound_max, view->var_place.data() );
				if( type == NC_DOUBLE )
					snprintf( tstr, 1499, "Current %s: %lg", dim_longname.c_str(), temp_double );
				else
					snprintf( tstr, 1499, "Current %s: %s", dim_longname.c_str(),
						tstr2 );
				if( !units.empty() ) {
					strncat( tstr, " ", sizeof(tstr) - strlen(tstr) - 1 );
					strncat( tstr, units.c_str(), sizeof(tstr) - strlen(tstr) - 1 );
					}
				fprintf( outf, "gsave (%s) show grestore\n", tstr );
				fprintf( outf, "0 %d rmoveto\n", -(printopts.leading+printopts.font_size) );
				}
			
		/*** Name of file ***/
		tstr[0] = '\0';
		std::array<size_t, 20> actual_place;
		virt_to_actual_place( view->variable, view->var_place.data(), actual_place.data(), &fdb );
		if( (fi_recdim_id( view->variable->files.front().get()->id ) != view->x_axis_id ) &&
		    (fi_recdim_id( view->variable->files.front().get()->id ) != view->y_axis_id))
			snprintf( tstr, 1499, "Frame %ld in ",
				actual_place[view->scan_axis_id]+1 );
		strncat( tstr, "File ", sizeof(tstr) - strlen(tstr) - 1 );
		strncat( tstr, fdb->filename.c_str(), sizeof(tstr) - strlen(tstr) - 1 );
		fprintf( outf, "gsave (%s) show grestore\n", tstr );
		fprintf( outf, "0 %d rmoveto\n", -(printopts.leading+printopts.font_size) );
		}

	if( printopts.include_id ) {
		sec_since_1970 = time(NULL);
		snprintf( tstr, 1499, "%s %s", get_login_name(), ctime(&sec_since_1970) );
		/* Make the id font a bit smaller */
		set_font( outf, printopts.font_name.c_str(), 
				(int)((float)printopts.font_size*ID_FONT_SIZE_SCALE) );
		fprintf( outf, "gsave %ld %ld translate 0 0 moveto\n", 
			center_x + (long)((float)x_size*output_scale/2.0) 
						+ printopts.font_size + printopts.leading,
			bot_of_image );
		fprintf( outf, "90 rotate (%s) show grestore\n", tstr );
		}

	/****** All done! *****/
	fprintf( outf, "\n\nshowpage\n" );
	fclose( outf );
	if( printopts.output_device == Device::Printer ) {
		/* Before executing the command, ensure that the file name exists ... helps
		 * to prevent problems if a strange file name is specified, such as "out.ps ; rm -r ."
		 */
		if( (f_dummy = fopen( printopts.out_file_name.c_str(), "r" )) == NULL ) {
			fprintf( stderr, "Error, could not open file \"%s\" for reading, which is a prerequisite to printing it\n",
				printopts.out_file_name.c_str() );
			exit( -1 );
			}
		fclose( f_dummy );
		snprintf( tstr, 1499, "lpr \"%s\"\n", printopts.out_file_name.c_str() );
		system( tstr );
		unlink( printopts.out_file_name.c_str() );
		}

	fprintf( stdout, "" );
	fflush( stdout );
	in_set_cursor_normal();
#ifdef DEBUG
	fprintf( stderr, "print_other_info: exiting\n" );
#endif
}

	static void
set_font( FILE *outf, const char *name, int size )
{
	fprintf( outf, "/%s findfont\n", name );
	fprintf( outf, "%d scalefont setfont\n", size );
}

	static void
calc_scale( float *scale, size_t x, size_t y )
{
	size_t	page_width, page_height;
	float	scale_x, scale_y;

	page_width = (printopts.page_width-2.0*printopts.page_x_margin)*printopts.ppi;
	page_height =   (printopts.page_height -
			   (printopts.page_upper_y_margin + printopts.page_lower_y_margin)
			)*printopts.ppi;

	scale_x = page_width / (float)x;
	scale_y = page_height / (float)y;

	*scale = (scale_x < scale_y) ? scale_x : scale_y;
}

	static void
do_outline( FILE *f, size_t x, size_t y )
{
	fprintf( f, "newpath\n" );
	fprintf( f, "0 0 moveto\n" );
	fprintf( f, "0 %ld lineto\n", -y );
	fprintf( f, "%ld %ld lineto\n", x, -y );
	fprintf( f, "%ld 0 lineto\n", x );
	fprintf( f, "0 0 lineto\n" );
	fprintf( f, "closepath stroke\n" );
}

	static void
print_header( FILE *f, float scale, size_t x, size_t y, size_t top_of_image )
{
	fprintf( f, "%%!\n" );
	fprintf( f, "/picstr %ld string def\n", x*3 );
	fprintf( f, "gsave\n" );

	/* This sets the position of the output image on the page */
	fprintf( f, "%ld %ld translate\n", 
			(long)(printopts.page_x_margin*printopts.ppi), top_of_image );

	/* This sets the size of the image */
	fprintf( f, "%f %f scale\n", scale, scale );

	if( printopts.test_only ) {
		fprintf( f, "newpath\n" );
		fprintf( f, "0 0 moveto\n" );
		fprintf( f, "0 %ld lineto\n", -y );
		fprintf( f, "%ld %ld lineto\n", x, -y );
		fprintf( f, "%ld 0 lineto\n", x );
		fprintf( f, "0 0 lineto\n" );
		fprintf( f, "%ld %ld lineto\n", x, -y );
		fprintf( f, "0 %ld moveto\n", -y );
		fprintf( f, "%ld 0 lineto\n", x );
		fprintf( f, "closepath stroke\n" );
		}
	else
		{
		fprintf( f, "%ld %ld 8\n", x, y );
		fprintf( f, "[1 0 0 -1 0 1]\n" );
		fprintf( f, "{currentfile picstr readhexstring pop}\n" );
		fprintf( f, "false 3\n" );
		fprintf( f, "colorimage\n\n" );
		}
}


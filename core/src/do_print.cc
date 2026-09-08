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

#define PAGE_X_MARGIN		1.5	/* Inches */
#define PAGE_UPPER_Y_MARGIN	2.0	/* Inches */
#define PAGE_LOWER_Y_MARGIN	4.0	/* Inches */

#define FONT_SIZE		11
#define HEADER_FONT_SIZE	16
#define FONT_NAME		"Helvetica"
#define LEADING			3

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

static void build_print_info( PrintInfo *info, size_t x_size, size_t y_size );

/********************************************************************/

	void
print_init( void )
{
	printopts.page_x_margin 	= PAGE_X_MARGIN;
	printopts.page_upper_y_margin 	= PAGE_UPPER_Y_MARGIN;
	printopts.page_lower_y_margin 	= PAGE_LOWER_Y_MARGIN;

	printopts.leading 		= LEADING;
	printopts.font_size		= FONT_SIZE;
	printopts.header_font_size 	= HEADER_FONT_SIZE;

	printopts.include_outline	= INCLUDE_OUTLINE;
	printopts.include_id		= INCLUDE_ID;
	printopts.include_title		= INCLUDE_TITLE;
	printopts.include_axis_labels	= INCLUDE_AXIS_LABELS;
	printopts.include_extra_info	= INCLUDE_EXTRA_INFO;

	printopts.test_only		= TEST_ONLY;

	printopts.font_name = FONT_NAME;
}

/*************************************************************************/

	void
do_print( void )
{
	size_t	x_size, y_size, scaled_x_size, scaled_y_size;

#ifdef DEBUG
	fprintf( stderr, "entering do_print()\n" );
#endif
	x_size = view->variable->size[view->x_axis_id];
	y_size = view->variable->size[view->y_axis_id];
	view_get_scaled_size( options.blowup, x_size, y_size, &scaled_x_size, &scaled_y_size );

	/* Lets the user tune the page layout -- margins, fonts, and which of
	 * the title/axis-labels/extra-info/outline/ID blocks to include.
	 * Where the page actually goes (printer vs file, paper, orientation)
	 * is the native print dialog's job, popped by in_print() below. */
	if( printer_options( &printopts ) == Message::Cancel )
		return;

	in_set_cursor_busy();

	view_draw( false, false ); /* Don't allow saveframes -- force reload of image data */

	PrintInfo info;
	info.width  = scaled_x_size;
	info.height = scaled_y_size;
	info.pixels = view->pixels.data();
	build_print_info( &info, scaled_x_size, scaled_y_size );

	in_print( info, printopts );

	in_set_cursor_normal();
#ifdef DEBUG
	fprintf( stderr, "exiting do_print()\n" );
#endif
}

/*************************************************************************/

	static void
build_print_info( PrintInfo *info, size_t x_size, size_t y_size )
{
	char 	*x_dim_name, *y_dim_name, tstr[1500], tstr2[1000], *dim_name;
	std::string units, x_dim_longname, y_dim_longname, x_units, y_units,
		main_long_name, main_units, dim_longname, file_title;
	FDBlist	*fdb;
	NCDim	*d;
	int	i, type, has_bounds;
	time_t	sec_since_1970;
	double	temp_double, bound_min, bound_max;

#ifdef DEBUG
	fprintf( stderr, "build_print_info: entering\n" );
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
		info->title = main_long_name;
		if( !main_units.empty() )
			info->title += " (" + main_units + ")";
	}

	/***** X/Y axis titles *****/
	if( printopts.include_axis_labels ) {
		info->x_axis_label = x_dim_longname;
		if( !x_units.empty() )
			info->x_axis_label += " (" + x_units + ")";

		info->y_axis_label = y_dim_longname;
		if( !y_units.empty() )
			info->y_axis_label += " (" + y_units + ")";
	}

	/***************** Other information *******************/
	if( printopts.include_extra_info ) {
		/**** File title ***/
		file_title = fi_title( view->variable->files.front().get()->id );
		if( !file_title.empty() )
			info->extra_info.push_back( file_title );

		/*** Range of data ***/
		snprintf( tstr, 1499, "Range of %s: %g to %g %s", main_long_name.c_str(),
			view->variable->user_min, view->variable->user_max, main_units.c_str() );
		info->extra_info.push_back( tstr );

		/*** Range of X axis ***/
		d = view->variable->dim[view->x_axis_id].get();
		if( x_units.empty() )
			snprintf( tstr, 1499, "Range of %s: %g to %g",
				x_dim_longname.c_str(), d->min, d->max);
		else
			snprintf( tstr, 1499, "Range of %s: %g to %g %s",
				x_dim_longname.c_str(), d->min, d->max, x_units.c_str() );
		info->extra_info.push_back( tstr );

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
		info->extra_info.push_back( tstr );

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
				info->extra_info.push_back( tstr );
				}

		/*** Name of file ***/
		tstr[0] = '\0';
		std::array<size_t, 20> actual_place;
		virt_to_actual_place( view->variable, view->var_place.data(), actual_place.data(), &fdb );
		if( (view->scan_axis_id != -1) &&
		    (fi_recdim_id( view->variable->files.front().get()->id ) != view->x_axis_id ) &&
		    (fi_recdim_id( view->variable->files.front().get()->id ) != view->y_axis_id))
			snprintf( tstr, 1499, "Frame %ld in ",
				(long)(actual_place[view->scan_axis_id]+1) );
		strncat( tstr, "File ", sizeof(tstr) - strlen(tstr) - 1 );
		strncat( tstr, fdb->filename.c_str(), sizeof(tstr) - strlen(tstr) - 1 );
		info->extra_info.push_back( tstr );
		}

	if( printopts.include_id ) {
		sec_since_1970 = time(NULL);
		snprintf( tstr, 1499, "%s %s", get_login_name(), ctime(&sec_since_1970) );
		info->id_stamp = tstr;
	}

#ifdef DEBUG
	fprintf( stderr, "build_print_info: exiting\n" );
#endif
}

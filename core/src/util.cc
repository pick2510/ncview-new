/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 2026 Dominik Strebel
 * Copyright (C) 1993 through 2024  David W. Pierce
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
 * davidwilliampierce@gmail.com
 */

/*******************************************************************************
 * 	util.c
 *
 *	utility routines for ncview
 *
 *	should be independent of both the user interface and the data
 * 	file format.
 *******************************************************************************/

/* This memory check makes things run slow */
/* define CHECK_MEM */

#include <vector>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "ncview/frame_renderer.h"

#include "math.h"

/*-------------------*/
#ifdef HAVE_UDUNITS2
#include "udunits2.h"
extern ut_system *unitsys;
#endif 
/*-------------------*/

extern Options   options;

static void handle_time_dim( int fileid, NCVar *v, int dimid );
static TimeGranularity  months_calc_tgran( int fileid, NCDim *d );
static float util_mean( float *x, size_t n, float fill_value );
static float util_mode( float *x, size_t n, float fill_value );
static void contract_data( float *small_data, View *v, float fill_value );
static int data_has_mv( float *data, size_t n, float fill_value );
static void handle_dim_mapping_scalar( NCVar *v, char *coord_var_name, char *coord_att );
static void handle_dim_mapping_2d( NCVar *v, char *coord_var_name, char *coord_att, 
	size_t *coord_var_eff_size, int coord_var_neff_dims, char *orig_coord_att,
	int ncid );
static int  determine_lat_lon( char *s_in, int *is_lat, int *is_lon );

/* Variables local to routines in this file */
static  const char *month_name[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/*******************************************************************************
 * Determine whether the data is "close enough" to the fill value
 */
int
close_enough( float data, float fill )
{
	float	criterion, diff;
	int	retval;

	if( fill == 0.0 )
		criterion = 1.0e-5;
	else if( fill < 0.0 )
		criterion = -1.0e-5*fill;
	else
		criterion = 1.0e-5*fill;

	diff = data - fill;
	if( diff < 0.0 ) 
		diff = -diff;

	if( diff <= criterion )
		retval = 1;
	else
		retval = 0;

 /* printf( "d=%f f=%f c=%f r=%d\n", data, fill, criterion, retval );  */
	return( retval );
}

/******************************************************************************
 * Allocate space for a NetCDFOptions structure. `new`, not malloc(): the
 * one caller (Dataset::new_fdblist(), dataset.cc) immediately hands the
 * result to a std::unique_ptr<NetCDFOptions>, whose default deleter calls
 * `delete`, not `free()`. This was a real malloc/delete mismatch --
 * caught by ASan's alloc-dealloc-mismatch check only once something
 * actually destroyed a Dataset mid-process (tests/support/
 * session_fixture.h's SessionFixture, "refine the architecture" plan's
 * Phase 0a): every previous test run left the global Dataset's teardown
 * to process exit, but tests/main.cc's ncviewTestsFastExit() calls
 * std::_Exit()/TerminateProcess() specifically to skip static destructors
 * (see fast_exit.h), so the mismatched delete had never actually run.
 */
	void
new_netcdf( NetCDFOptions **n )
{
	(*n) = new NetCDFOptions();
	(*n)->valid_range_set  = false;
	(*n)->valid_min_set    = false;
	(*n)->valid_max_set    = false;
	(*n)->scale_factor_set = false;
	(*n)->add_offset_set   = false;

	(*n)->valid_range[0] = 0.0;
	(*n)->valid_range[1] = 0.0;
	(*n)->valid_min      = 0.0;
	(*n)->valid_max      = 0.0;
	(*n)->scale_factor   = 1.0;
	(*n)->add_offset     = 0.0;
}

/******************************************************************************
 * Return 1 if any data value is missing, 0 otherwise
 */
	int
data_has_mv( float *data, size_t n, float fill_value )
{
	size_t i;

	for( i=0; i<n; i++ )
		if( close_enough( data[i], fill_value ))
			return(1);

	return(0);
}

/******************************************************************************
 * Scale the data, replicate it, and convert to a pixel type array.  I'm afraid
 * that for speed, this considers 'ncv_pixel' to be a single byte value.  Make sure
 * to change it if you change the definition of ncv_pixel!  Returns 0 on
 * success, -1 on failure.
 */
	int
data_to_pixels( View *v )
{
	size_t	i;
	size_t	x_size, y_size, new_x_size, new_y_size;
	float	fill_value;
	std::vector<float> scaled_data;
	long	blowup;
	Message	result;
	MinMaxMethod	orig_minmax_method;
	char	error_message[1024];

	/* Make sure the limits have been set on this variable.
	 * They won't always be because an initial expose event can 
	 * cause this routine to be executed before the min and
	 * maxes are calcuclated.
	 */
	if( ! v->variable->have_set_range )
		return( -1 );

	blowup   = options.blowup;	/* NOTE: can be negative if shrinking data! -N means size is 1/Nth */

	x_size     = v->variable->size[v->x_axis_id];
	y_size     = v->variable->size[v->y_axis_id];

	view_get_scaled_size( options.blowup, x_size, y_size, &new_x_size, &new_y_size );

	/* std::vector's own allocator throws std::bad_alloc on failure (there is
	 * no NULL-check equivalent to preserve); everything downstream is
	 * otherwise identical to the old malloc()'d buffer. */
	scaled_data.resize( new_x_size*new_y_size );

	/* If we are doing overlays, implement them */
	if( options.overlay->doit && (! options.overlay->overlay.empty())) {
		for( i=0; i<(x_size*y_size); i++ ) {
			v->data[i] =
			     (float)(1 - options.overlay->overlay[i]) * v->data[i] +
			     (float)(options.overlay->overlay[i]) * v->variable->fill_value;
			}
		}

	fill_value = v->variable->fill_value;

	if( blowup > 0 ) {
		if( options.debug ) printf( "..expanding data, blowup=%ld\n", blowup );
		expand_data( scaled_data.data(), v, new_x_size*new_y_size );
		}
	else
		{
		if( options.debug ) printf( "..contracting data, blowup=%ld\n", blowup );
		contract_data( scaled_data.data(), v, fill_value );
		}

	if( (v->variable->user_max == 0) &&
	    (v->variable->user_min == 0) &&
	    (! options.autoscale) ) {
		in_set_cursor_normal();
		do_pause( Modifier::M1 );	/* pause playback directly -- no need to round-trip through the Button enum */
		if( options.min_max_method == MinMaxMethod::Exhaust ) {
	    		snprintf( error_message, 1022, "min and max both 0 for variable %s (checked all data)\nSetting range to (-1,1)", 
								v->variable->name.c_str() );
			in_error( error_message );
			v->variable->user_max = 1;
			v->variable->user_min = -1;
			v->variable->auto_set_no_range = 1;
			return( data_to_pixels(v) );
			}
	    	snprintf( error_message, 1022, "min and max both 0 for variable %s.\nI can check ALL the data instead of subsampling if that's OK,\nor just cancel viewing this variable.",
	    				v->variable->name.c_str() );
		result = in_dialog( error_message, true );
		if( result == Message::OK ) {
			orig_minmax_method = options.min_max_method;
			options.min_max_method = MinMaxMethod::Exhaust;
			g_dataset.initMinMax( v->variable );
			options.min_max_method = orig_minmax_method;
			if( (v->variable->user_max == 0) &&
	    		    (v->variable->user_min == 0) ) {
	    			snprintf( error_message, 1022, "min and max both 0 for variable %s (checked all data)\nSetting range to (-1,1)", 
								v->variable->name.c_str() );
				in_error( error_message );
				v->variable->user_max = 1;
				v->variable->user_min = -1;
				v->variable->auto_set_no_range = 1;
				return( data_to_pixels(v) );
				}
			else
				return( data_to_pixels(v) );
			}
		else
			{
			if( ! data_has_mv( v->data.data(), x_size*y_size, fill_value ) )
				return( -1 );
			v->variable->user_max = 1;
			}
	    	}

	if( (v->variable->user_max == v->variable->user_min) && (! options.autoscale) ) {
		in_set_cursor_normal();
	    	snprintf( error_message, 1022, "min and max both %g for variable %s",
	    		v->variable->user_min, v->variable->name.c_str() );
		x_error( error_message );
		if( ! data_has_mv( v->data.data(), x_size*y_size, fill_value ) ) {
			v->variable->user_max += 0.1 * v->variable->user_max;
			v->variable->user_min -= 0.1 * v->variable->user_min;
			v->variable->auto_set_no_range = 1;
			return( data_to_pixels(v) );
			}
		/* If we get here, data is all same, but have a missing value,
		 * so let's go ahead and show it
		 */
		if( v->variable->user_max == 0 )
			v->variable->user_max = 1;
		else if( v->variable->user_max > 0 )
			v->variable->user_min = 0;
		else
			v->variable->user_max = 0;
	    	}

	PixelMapSettings pixel_map_settings = g_app.session.pixelMapSettings( options );
	FrameRenderer::render( scaled_data.data(), new_x_size, new_y_size,
		fill_value, v->variable->user_min, v->variable->user_max,
		pixel_map_settings, pixel_transform, v->pixels.data() );

	return( 0 );
}

/******************************************************************************
 * Returns the number of entries in the NCVarlist
 */
	int
n_vars_in_list( const std::vector<std::unique_ptr<NCVar>> &v )
{
	return( (int)v.size() );
}

/******************************************************************************
 * Clip out of range floats 
 */
	void
clip_f( float *data, float min, float max )
{
	if( *data < min )
		*data = min;
	if( *data > max )
		*data = max;
}

/******************************************************************************
 * Turn a virtual variable 'place' array into a file/place pair.  Which is
 * to say, the virtual size of a variable spans the entries in all the files; 
 * the actual place where the entry for a particular virtual location can
 * be found is in a file/actual_place pair.  This routine does the conversion.
 * Note that this routine is assuming the netCDF convention that ONLY THE
 * FIRST index can be contiguous across files.  The first index is typically
 * the time index in netCDF files.  NOTE! that 'act_pl' must be allocated 
 * before calling this!
 */
	void
virt_to_actual_place( NCVar *var, size_t *virt_pl, size_t *act_pl, FDBlist **file )
{
	FDBlist	*f;
	size_t	v_place, cur_start, cur_end;
	size_t	file_idx;
	int	i, n_dims;

	f       = var->files.front().get();
	n_dims  = fi_n_dims( f->id(), const_cast<char *>(var->name.c_str()) );
	v_place = *(virt_pl);

	if( v_place >= var->size[0] ) {
		fprintf( stderr, "ncview: virt_to_actual_place: error trying ");
		fprintf( stderr, "to convert the following virtual place to\n" );
		fprintf( stderr, "an actual place for variable %s:\n", var->name.c_str() );
		for( i=0; i<n_dims; i++ )
			fprintf( stderr, "[%1d]: %zu\n", i, *(virt_pl+i) );
		exit( -1 );
		}

	file_idx  = 0;
	cur_start = 0L;
	cur_end   = f->var_size[0] - 1L;
	while( v_place > cur_end ) {
		cur_start += f->var_size[0];
		file_idx++;
		f          = var->files[file_idx].get();
		cur_end   += f->var_size[0];
		}

	*file = f;
	*act_pl = v_place - cur_start;

	/* Copy the rest of the indices over */
	for( i=1; i<n_dims; i++ )
		*(act_pl+i) = *(virt_pl+i);
}

/******************************************************************************
 * Initialize the var->dim_map_info table
 */
	void
handle_dim_mapping( NCVar *v )
{
	int	i, varid, ncid, coord_var_ndims, coord_var_neff_dims;
	size_t	coord_var_eff_size[MAX_NC_DIMS];
	char	*s, orig_coord_att[1024];
	const 	char *delim = " \n\0\t";

	if( options.debug ) printf( "handle_dim_mapping: entering for var %s\n", v->name.c_str() );

	ncid = v->files.front()->id();

	/* dim_map_info itself is never empty of entries. If the var has no coordinate
	 * mappings, then every entry stays a null unique_ptr.
	 */
	v->dim_map_info.clear();
	v->dim_map_info.resize( v->n_dims );

	/* See if this var has a "coordinates" attribute */
	/* coord_att is strtok()'d in place below and its address (with tokens
	 * already split out) is threaded on into handle_dim_mapping_scalar()/
	 * _2d() -- both still take a raw char*, so bridge with strdup() the
	 * same way the old malloc'd netcdf_get_char_att() return did (still
	 * never freed either way, matching prior behavior). */
	std::string coord_att_s = netcdf_get_char_att( ncid, v->name, "coordinates" );
	if( coord_att_s.empty() )
		return;
	char *coord_att = strdup( coord_att_s.c_str() );

	snprintf( orig_coord_att, sizeof(orig_coord_att), "%s", coord_att );
	if( options.debug ) printf( "var %s HAS a coordinates attribute: >%s<\n", v->name.c_str(), coord_att );

	/* Check for blank-delimited strings in the coordinates attribute
	 * that name other vars in the file
	 */
	s = strtok( coord_att, delim );
	while( s != NULL ) {

		/* See if this token "s", which came from the coordinates attribute,
		 * is the name of a variable in the file
		 */
		varid = safe_ncvarid( ncid, s );
		if( varid != -1 ) {	/* yes, the token "s" matches the name of a var in the file! */

			/* Right now, I'm only going to try to handle either scalar (0d)
			 * or 2-D mapping dims.  Scalar mapping dims just give an
			 * additional location where the data is valid; for example,
			 * the data might be a 2d field in (lon,lat) and have a
			 * "height" coordinate that tells the height the data is at.
			 * If the dim is more complicated than that, then we simply
			 * ignore the mapping.  In particular, the test WRF output file
			 * I have has a 3-D mapping dim with time as the first time.
			 * Does WRF move the mapping around over time?  Dunno.  In
			 * any event, we will allow it to handle 2 EFFECTIVE dims,
			 * but otherwise, if the mapping var has more than 2 effective
			 * dims, then forget it.
			 */
			coord_var_ndims = netcdf_n_dims( ncid, s );
			size_t *coord_var_size_raw = netcdf_fi_var_size( ncid, s );
			coord_var_neff_dims = 0;
			for( i=0; i<coord_var_ndims; i++ )
				if( coord_var_size_raw[i] > 1 ) {
					coord_var_eff_size[coord_var_neff_dims] = coord_var_size_raw[i];
					coord_var_neff_dims++;
					}
			free( coord_var_size_raw );

			/* These routines are where we do most of the work
			 */
			if( coord_var_neff_dims == 0 )
				handle_dim_mapping_scalar( v, s, coord_att );

			else if( coord_var_neff_dims == 2 )
				handle_dim_mapping_2d( v, s, coord_att, coord_var_eff_size, coord_var_neff_dims,
					orig_coord_att, ncid );

			else
				{
				printf( "Note: the coordinates attribute for variable %s is being ignored,\n", v->name.c_str() );
				printf( "since it specifies a variable (%s) that has %d effective dims (an effective dim has a size greater than 1)\n",
					s, coord_var_neff_dims );
				printf( "I am not set up to handle cases with coordinate mapping using anything other than 0 or 2 effective dims\n" );
				return;
				}
			}
		else
			{
			/* varid == -1, indicating no var of this name was found in the file */
			if( options.debug )
				printf( "Warning: token \"%s\" appears in a coordinates attribute yet is NOT a var in the file\n", s );
			}

		s = strtok( NULL, delim );
		}
}

/**********************************************************************************************/
	static void
handle_dim_mapping_scalar( NCVar *v, char *coord_var_name, char *coord_att )
{
	if( (int)v->scalar_dim_map_info.size() >= MAX_SCALAR_COORDS ) {
		printf( "Note: var %s has exceeded the allowable number of scalar coordinate dimensions, which is %d. Ignoring the rest\n",
			v->name.c_str(), MAX_SCALAR_COORDS);
		return;
		}

	/* Make a new SCALAR dim map info structure to
	 * hold our single value
	 */
	auto tmi_owner = std::make_unique<NCDim_map_info>();
	NCDim_map_info *tmi = tmi_owner.get();

	/* Copy info to the new scalar dim map structure
	 */
	tmi->var_i_map = v;
	tmi->coord_var_name = coord_var_name;
	/* tmi->coord_var_units is printed with a bare "%s" with no NULL guard
	 * in view.cc, so an absent-units result must stay empty here rather
	 * than something that would render as blank text either way -- the
	 * empty-string convention is unambiguous now that this is std::string. */
	tmi->coord_var_units = fi_var_units( v->files.front()->id(), coord_var_name );
	tmi->scalar_all_same = 0;

	/* Same check handle_time_dim() runs for a real dimension's own units --
	 * lets a scalar coordinate like WRF's "XTIME" (units "minutes since
	 * ...") be printed as a calendar date later, instead of the raw
	 * "<value> <units>" string this used to always fall back to. */
	if( udu_utistime( coord_var_name, const_cast<char *>(tmi->coord_var_units.c_str()) ) ) {
		tmi->timelike = 1;
		tmi->calendar = fi_dim_calendar( v->files.front()->id(), coord_var_name );
		}

	/* Add this new scalar dim to the array */
	v->scalar_dim_map_info.push_back( std::move( tmi_owner ) );

	if( options.debug ) printf("Added a new scalar coord to var %s: name=%s count=%zu\n",
		v->name.c_str(), coord_var_name, v->scalar_dim_map_info.size() );

	/* Note that we CANNOT fill out the data values for this "scalar" coord var
	 * yet. The reason is because this var might live in multiple files, and
	 * each file could have a different value of the scalar variable. I.e.,
	 * the scalar coord var could be used to essentially add another unlimited
	 * dimension to the variable. To handle this, we must read in the
	 * scalar values from EACH FILE after we know what all the files this
	 * variable lives in are. That happens in the routine that calls this
	 * one, add_var_to_list
	 */
}

/****************************************************************************************************/
	static void
handle_dim_mapping_2d( NCVar *v, char *coord_var_name, char *coord_att, size_t *coord_var_eff_size,
		int coord_var_neff_dims, char *orig_coord_att, int ncid )
{
	size_t		totsize, start[MAX_NC_DIMS], count[MAX_NC_DIMS];
	int		i, n_matches, err, is_lat, is_lon, idx_lat_dim, idx_lon_dim,
			must_be_left_of, j;

	/* Make new, uninitialized dim_map_info structure. Ownership transfers into
	 * v->dim_map_info[i] below on every path except an early "abandon mapping"
	 * return, where the local unique_ptr cleans it up automatically. */
	auto map_info_owner = std::make_unique<NCDim_map_info>();
	NCDim_map_info *map_info = map_info_owner.get();

	/* Copy over the coordinate attribute for posterity */
	map_info->coord_att = coord_att;

	/* This is the "variable that I map" */
	map_info->var_i_map = v;

	/* Since "coord_var_name" matches a var name, it must be the coordinate variable name in particular.
	 * The coordinate variable is the var in the file that holds mapping info
	 */
	map_info->coord_var_name = coord_var_name;

	if( options.debug ) printf( "Coord var named >%s< is a NON-SCALAR coord used to map a dimension of var %s\n",
			coord_var_name, v->name.c_str() );

	/* See how many dims this coord var has */
	map_info->coord_var_ndims = netcdf_n_dims( ncid, coord_var_name );

	/* Get size of the coord var */
	{
	size_t *raw = netcdf_fi_var_size( ncid, coord_var_name );
	map_info->coord_var_size.assign( raw, raw + map_info->coord_var_ndims );
	free( raw );
	}

	if( options.debug ) {
		printf( "non-scalar Coord var %s has %d dims, here are their sizes: ",
			coord_var_name, map_info->coord_var_ndims );
		for( i=0; i<map_info->coord_var_ndims; i++ )
			printf( "%zu ", map_info->coord_var_size[i] );
		printf( "\n" );
		}

	/* Get array of boolean indicating which dims in the
	 * base var match the shape of this coord var.  For
	 * instance, if we have a var of shape (10,20,180,360)
	 * and a coord var of shape (180,360) then this indicating
	 * array will be 0,0,1,1.
	 */
	map_info->matching_var_dims.assign( v->n_dims, 0 );
	map_info->index_place_factor.assign( v->n_dims, 0 );

	/* We could have a problem if the dim sizes are repeated instead of unique.
	 * For example, imagine a square data array of size [n,n].  Then we have
	 * a lon mapping array of size [n,n].  We don't want the boolean array
	 * 'matching_var_dims' to end up as [0,1], we want it to end up as [1,1].
	 * In other words, stop a dim in the coord var from matching the same dim
	 * in the original var twice, even if the dim size is repeated.  We do this
	 * by fist finding a match, then requiring the NEXT match to be to the
	 * left of (in the array of the var's dim sizes) the previous match.
	 */
	must_be_left_of = v->n_dims;	/* Start out by setting all the way to right edge */
	for( i=map_info->coord_var_ndims-1; i>=0; i--) {/* Want to find a dim in v that matches size of coord_var dim number i... */
		/*
		printf( "Searching for a dim in var %s that matches dim number %d in %s, which is of size %d\n",
			v->name, i, s, coord_var_eff_size[i] );
		printf( "the match must be to the left of %d\n", must_be_left_of );
		*/
		for( j=must_be_left_of-1; j>=0; j-- ) {	/* ...subject to constraint that match be left of (have lower numerical value then) j */
			if( coord_var_eff_size[i] == v->size[j] ) {
				map_info->matching_var_dims[j] = 1;
				must_be_left_of = j;	/* found a match at j, so NEXT match must be at a lower value of j than this */
				break;
				}
			}
		}

	n_matches = 0;
	for( i=0; i<v->n_dims; i++ )
		n_matches += map_info->matching_var_dims[i];
	if( n_matches != coord_var_neff_dims ) {
		fprintf( stderr, "Warning: did not correctly match mapped dims specified in the coordinates attribute to dims in the variable\n" );
		fprintf( stderr, "Problem encountered on variable \"%s\" which has shape (", v->name.c_str() );
		for( i=0; i<v->n_dims; i++ ) {
			fprintf( stderr, "%zu", v->size[i] );
			if( i < (v->n_dims-1))
				fprintf( stderr, "," );
			}
		fprintf( stderr, ")\n" );
		fprintf( stderr, "and has coordinates attribute \"%s\"\n", orig_coord_att );
		fprintf( stderr, "The problem is that coordinate var \"%s\" has shape (", coord_var_name );
		for( i=0; i<map_info->coord_var_ndims; i++ ) {
			fprintf( stderr, "%zu", map_info->coord_var_size[i] );
			if( i < (map_info->coord_var_ndims-1))
				fprintf( stderr, "," );
			}
		fprintf( stderr, "), which does not match dimensions in the variable being mapped!\n" );
		fprintf( stderr, "Abandoning coordinate mapping for this variable\n-------------\n" );
		for( i=0; i<v->n_dims; i++ )
			v->dim_map_info[i].reset();
		return;
		}
	if( (n_matches<1) || (n_matches>2)) {
		fprintf( stderr, "(Location B) Error, did not correctly match mapped dims specified in the coordinates attribute to dims in the variable\n" );
		fprintf( stderr, "(Location B) Please send email to dpierce@ucsd.edu letting me know what your coordinates attribute looks like so I can fix this problem.\n" );
		fprintf( stderr, "Problem encountered on variable \"%s\"\n", v->name.c_str() );
		fprintf( stderr, "which has coordinates attribute \"%s\"\n", orig_coord_att );
		fprintf( stderr, "Abandoning coordinate mapping for this variable\n" );
		for( i=0; i<v->n_dims; i++ )
			v->dim_map_info[i].reset();
		return;
		}

	/* Try to figure out if this dim is 'latitude' like
	 * or 'longitude' like....these are the only options
	 * for now.
	 */
	err = determine_lat_lon( const_cast<char *>(map_info->coord_var_name.c_str()), &is_lat, &is_lon );
	if( err != 0 ) {
		/* Abort this process */
		for( i=0; i<v->n_dims; i++ )
			v->dim_map_info[i].reset();
		return;
		}
	idx_lon_dim = -1;
	idx_lat_dim = -1;
	if( is_lon ) {
		if( options.debug ) printf( "Coord var was found to be a LONGITUDE\n" );
		/* Match this coord var to the last one on the right */
		for( i=v->n_dims-1; i>=0; i-- ) {
			if( map_info->matching_var_dims[i] == 1 ) {
				if( options.debug )
					printf( "In variable \"%s\", dimension \"%s\" is mapped by LONGITUDE-like %d-dimensional variable \"%s\"\n",
					v->name.c_str(), netcdf_dim_id_to_name( v->files.front()->id(), v->name, i).c_str(),
					map_info->coord_var_ndims, map_info->coord_var_name.c_str() );
				v->dim_map_info[i] = std::move( map_info_owner );
				idx_lon_dim = i;
				break;
				}
			}
		/* Now, since we've found the index of the lon dim, the
		 * index of the lat dim must be the other one
		 */
		for( i=0; i<v->n_dims; i++ )
			if( (map_info->matching_var_dims[i] == 1) && (i != idx_lon_dim))
				idx_lat_dim = i;
		}
	else if( is_lat ) {
		if( options.debug ) printf( "Coord var was found to be a LATITUDE\n" );
		/* Match this coord var to the first one on the left */
		for( i=0; i<v->n_dims; i++ ) {
			if( map_info->matching_var_dims[i] == 1 ) {
				idx_lat_dim = i;
				if( options.debug )
					printf( "In variable \"%s\", dimension \"%s\" is mapped by LATITUDE-like dimension %d-dimensional variable \"%s\"\n",
					v->name.c_str(), netcdf_dim_id_to_name( v->files.front()->id(), v->name, i).c_str(),
					map_info->coord_var_ndims, map_info->coord_var_name.c_str() );
				v->dim_map_info[i] = std::move( map_info_owner );
				break;
				}
			}
		/* Now, since we've found the index of the lat dim, the
		 * index of the lon dim must be the other one
		 */
		for( i=0; i<v->n_dims; i++ )
			if( (map_info->matching_var_dims[i] == 1) && (i != idx_lat_dim))
				idx_lon_dim = i;
		}
	else
		{
		fprintf( stderr, "(Location C)Error, did not correctly match mapped dims specified in the coordinates attribute to dims in the variable\n" );
		fprintf( stderr, "(Location C)Please send email to dpierce@ucsd.edu letting me know what your coordinates attribute looks like so I can fix this problem.\n" );
		exit( -1 );
		}

	/* Read in data from var, store it in cache */
	totsize = 1L;
	for( i=0; i<map_info->coord_var_ndims; i++ ) {
		totsize *= map_info->coord_var_size[i];
		start[i] = 0L;
		count[i] = map_info->coord_var_size[i];
		}
	map_info->data_cache.resize( totsize );
	netcdf_fi_get_data( ncid, const_cast<char *>(map_info->coord_var_name.c_str()), start, count, map_info->data_cache.data(), NULL );

	if( n_matches == 1 ) {
		if( idx_lon_dim == -1 )
			map_info->index_place_factor[idx_lat_dim] = 1L;
		else
			map_info->index_place_factor[idx_lon_dim] = 1L;
		}
	else if( n_matches == 2 ) {
		map_info->index_place_factor[idx_lon_dim] = 1L;
		map_info->index_place_factor[idx_lat_dim] = v->size[ idx_lon_dim ];
		}
	else
		{
		fprintf( stderr, "(Location D)Error, did not correctly match mapped dims specified in the coordinates attribute to dims in the variable\n" );
		fprintf( stderr, "(Location D)Please send email to dpierce@ucsd.edu letting me know what your coordinates attribute looks like so I can fix this problem.\n" );
		exit( -1 );
		}

	/*
	printf( "matching var dims: " );
	for( i=0; i<v->n_dims; i++ )
		printf( "%d ", map_info->matching_var_dims[i] );
	printf( "Index place factor: " );
	for( i=0; i<v->n_dims; i++ )
		printf( "%zu ", map_info->index_place_factor[i] );
	printf( "\n" );
	*/
}

/******************************************************************************
 * Initialize all the fields in the dim structure by reading from the data file
 */
	void
fill_dim_structs( NCVar *v )
{
	int	i, fileid, debug;
	NCDim	*d;
	std::string dim_name, tmp_units;
	static  int global_id = 0;

	debug = 0;

	if( debug == 1 ) printf( "fill_dim_structs: entering for var %s, which has %d dims\n", v->name.c_str(), v->n_dims );

	fileid = v->files.front()->id();
	v->dim.clear();
	v->dim.resize( v->n_dims );
	for( i=0; i<v->n_dims; i++ ) {
		dim_name = fi_dim_id_to_name( fileid, v->name, i );
		if( debug == 1 ) printf( "fill_dim_structs: dim %d has name %s and length %zu\n", i, dim_name.c_str(), v->size[i] );
		if( is_scannable( v, i ) ) {
			v->dim[i] = std::make_unique<NCDim>();
			d            	= v->dim[i].get();
			d->name      	= dim_name;
			d->long_name 	= fi_dim_longname( fileid, dim_name );
			d->have_calc_minmax = 0;
			d->units = fi_dim_units( fileid, dim_name );
			d->units_change = 0;
			d->size      	= v->size[i];
			d->calendar = fi_dim_calendar( fileid, dim_name );
			d->global_id 	= ++global_id;
			handle_time_dim( fileid, v, i );
			if( options.debug )
				printf( "adding scannable dim to var %s: dimname: %s dimsize: %zu\n", v->name.c_str(), dim_name.c_str(), d->size );
			}
		else
			{
			/* Indicate non-scannable dimensions by a null entry */
			v->dim[i].reset();
			if( options.debug )
				printf( "adding non-scannable dim to var %s: dim name: %s size: %zu\n",
					v->name.c_str(), fi_dim_id_to_name( fileid, v->name, i).c_str(), v->size[i] );
			}
		}

	/* If this variable lives in more than one file, it might have
	 * different time units in each one.  Check for this.
	 */
	if( v->is_virtual && (v->dim[0] != nullptr) && (v->files.size() > 1) ) {
		/* The timelike dimension MUST be the first one! */
		d = v->dim[0].get();
		if( d->timelike ) {
			/* Go through each file and see if it has the same units
			 * as the first file, which is stored in d->units.
			 *
			 * Upstream walked this via cursor->next on a linked list
			 * without ever advancing cursor -- an infinite loop the
			 * moment it triggered, never hit in practice because it
			 * requires >1 file AND a timelike first dimension AND
			 * (for the printed warning) differing units, an unlikely
			 * combination that apparently went unnoticed upstream.
			 * v->files is a vector here, so just index it instead of
			 * carrying that bug forward. */
			for( size_t ifile = 1; ifile < v->files.size(); ifile++ ) {
				tmp_units = fi_dim_units( v->files[ifile]->id(), d->name );
				if( d->units != tmp_units ) {
					printf( "** Warning: different time units found in different files.  Trying to compensate...\n" );
					d->units_change = 1;
					}
				}
			}
		}
}

/******************************************************************************
 * Is this a "scannable" dimension -- i.e., accessable by the taperecorder
 * style buttons? Is scannable if: 
 * 	> is unlimited
 *	> or, is size > 1
 */
	int
is_scannable( NCVar *v, int i )
{
	/* The unlimited record dimension is always scannable */
	if( i == 0 )
		return( true );

	if( v->size[i] > 1 )
		return( true );
	else
		return( false );
}

/******************************************************************************
 * Return the mode (most common value) of passed array "x".  We assume "x"
 * contains the floating point representation of integers.
 */
	float
util_mode( float *x, size_t n, float fill_value )
{
	long 	i, n_vals;
	long 	ival, max_count;
	std::vector<long> count_vals, unique_vals;
	int	foundval, j, max_index;
	float	retval;

	count_vals.resize( n );
	unique_vals.resize( n );

	n_vals = 0;
	for( i=0L; i<(long)n; i++ ) {
		if( close_enough( x[i], fill_value )) {
			return( fill_value );
			}
		ival = (x[i] > 0.) ? (long)(x[i]+.4) : (long)(x[i]-.4); /* round x[i] to nearest integer */
		foundval = -1;
		for( j=0; j<n_vals; j++ ) {
			if( unique_vals[j] == ival ) {
				foundval = j;
				break;
				}
			}
		if( foundval == -1 ) {
			unique_vals[n_vals] = ival;
			count_vals[n_vals] = 1;
			n_vals++;
			}
		else
			count_vals[foundval]++;
		}

	max_count = -1;
	max_index = -1;
	for( i=0L; i<n_vals; i++ )
		if( count_vals[i] > max_count ) {
			max_count = count_vals[i];
			max_index = i;
			}

	retval = (float)unique_vals[max_index];

	return( retval );
}

/******************************************************************************/
	float
util_mean( float *x, size_t n, float fill_value )
{
	size_t i;
	double sum;

	sum = 0.0;
	for( i=0L; i<n; i++ ) {
		if( close_enough( x[i], fill_value ))
			return( fill_value );
		sum += x[i];
		}

	sum = sum / (double)n;
	return( sum );
}

/********************************************************************************
 * Actually do the "shrinking" of the FLOATING POINT (not pixel) data, converting
 * it to the small version by either finding the most common value in the square,
 * or by averaging over the square.  Remember that our standard for how to 
 * interpret 'options.blowup' is that a value of "-N" means to shrink by a factor
 * of N.  So, blowup == -2 means make it half size, -3 means 1/3 size, etc.
 */
	void
contract_data( float *small_data, View *v, float fill_value )
{
	long 	n, ii, jj;
	size_t	i, j, nx, ny;
	size_t	new_nx, new_ny, idx, ioffset, joffset;
	float 	*tmpv;

	if( options.blowup > 0 ) {
		fprintf( stderr, "internal error, contract_data called with a positive blowup factor!\n" );
		exit(-1);
		}

	n = -options.blowup;
	std::vector<float> tmpv_buf( n*n );
	tmpv = tmpv_buf.data();

	/* Get old and new sizes (new size is smaller in this routine) */
	nx   = v->variable->size[v->x_axis_id];
	ny   = v->variable->size[v->y_axis_id];
	view_get_scaled_size( options.blowup, nx, ny, &new_nx, &new_ny );

	for( j=0; j<new_ny; j++ )
	for( i=0; i<new_nx; i++ ) {
		for( jj=0; jj<n; jj++ )
		for( ii=0; ii<n; ii++ ) {
			ioffset = i*n + ii;
			joffset = j*n + jj;
			if( ioffset >= nx )
				ioffset = nx-1;
			if( joffset >= ny )
				joffset = ny-1;
			idx = ioffset + joffset*nx;
			tmpv[ii + jj*n] = v->data[idx];
			}

		if( options.shrink_method == ShrinkMethod::Mean )
			small_data[i + j*new_nx] = util_mean( tmpv, n*n, fill_value );

		else if( options.shrink_method == ShrinkMethod::Mode ) {
			small_data[i + j*new_nx] = util_mode( tmpv, n*n, fill_value );
			}
		else
			{
			fprintf( stderr, "Error in contract_data: unknown value of options.shrink_method!\n" );
			exit( -1 );
			}
		}
}

/******************************************************************************
 * Actually do the "blowup" of the FLOATING POINT (not pixel) data, converting 
 * it to the large version by either interpolation or replication.
 * NOTE this routine is only called when options.blowup > 0!
 */
	void
expand_data( float *big_data, View *v, size_t array_size )
{
	size_t	idx, nxl, nyl, nxb, nyb;
	long	line, il, jl, i2b, j2b;
	int	blowup, offset_xb, offset_yb, miss_base, miss_right, miss_below;
	float	step, final_est, extrap_fact;
	float	base_val, right_val, below_val, val, bupr;
	float	base_x, base_y, del_x, del_y;
	float	est1, est2, frac_x, frac_y;
	float 	fill_val, cval;

	blowup   = options.blowup;

#ifdef CHECK_MEM
	printf( "...CHECK_MEM is on!!\n" );
#endif

	/*--------------------------------------------------------------------------------
	 * See my notebook entry of 2010-08-23. 
	 * In general we draw a distinction between indices that are valid in the
	 * original (little) array, indicazted by a "l" (little) suffix (such as il or jl),
	 * and indices valid in the destination (big) array, which have a suffix of "b".
	 *---------------------------------------------------------------------------------*/
	nxl = v->variable->size[v->x_axis_id];	/* # of X entries in the little array */
	nyl = v->variable->size[v->y_axis_id];	/* # of Y entries in the little array */
	nxb = nxl*blowup;				/* # of X entries in big array */
	nyb = nyl*blowup;				/* # of Y entries in big array */

	fill_val = v->variable->fill_value;
	
	if( (nxb < (size_t)blowup) || (nxb*nyb < (size_t)blowup) ) {
		fprintf( stderr, "ncview: data_to_pixels: too much magnification\n" );
		fprintf( stderr, "nxb=%zu\n", nxb );
		exit( -1 );
		}

	if( (blowup == 1) || (options.blowup_type == BlowupType::Replicate)) { 
		for( jl=0; jl<(long)nyl; jl++ ) {
			for( il=0; il<(long)nxl; il++ )
				for( i2b=0; i2b<blowup; i2b++ ) {
#ifdef CHECK_MEM
					if( il*blowup + jl*nxb*blowup + i2b >= array_size ) { fprintf( stderr, "mem error 001\n" ); exit(-1); }
#endif
					*(big_data + il*blowup + jl*nxb*blowup + i2b) = v->data[il+jl*nxl];
					}
			for( line=1; line<blowup; line++ )
				for( i2b=0; i2b<(long)nxb; i2b++ ) {
#ifdef CHECK_MEM
					if( i2b + jl*nxb*blowup + line*nxb >= array_size ) { fprintf( stderr, "mem error 002\n" ); exit(-1); }
#endif
					*(big_data + i2b + jl*nxb*blowup + line*nxb) =
						*(big_data + i2b + jl*nxb*blowup);
					}
			}
		} 

	else 	{ /* BlowupType::Bilinear */
		bupr = 1.0/(float)blowup;

		/* Offset where we will put the center value into the big array. These are offsets
		 * into the big array.
		 */
		offset_xb = (blowup - 1)/2;
		offset_yb = offset_xb;

		/* Horizontal base lines */
		for( jl=0; jl<(long)nyl; jl++ ) {
			for( il=0; il<(long)nxl-1; il++ ) {
				base_val  = v->data[il   + jl*nxl];
				right_val = v->data[il+1 + jl*nxl];

				miss_base  = close_enough(base_val,  fill_val);
				miss_right = close_enough(right_val, fill_val);
				if( miss_base ) {
					if( miss_right ) {
						/* BOTH missing */
						step = 0.0;
						val = base_val;		/* missing value */
						}
					else
						{
						/* base missing, but right is there */
						step = 0.0;
						val = right_val;	/* an OK value */
						}
					}
				else if( miss_right ) {
					/* ONLY right is missing, checked for both missing above */
					val = base_val;
					step = 0.0;
					}
				else
					{
					/* NEITHER missing */
					step = (right_val-base_val)*bupr;
					val = base_val;
					}

				for( i2b=0; i2b < blowup; i2b++ ) {
#ifdef CHECK_MEM
					if( il*blowup+i2b+offset_xb + jl*blowup*nxb + offset_yb*nxb >= array_size ) { fprintf( stderr, "mem error 003\n" ); exit(-1); }
#endif
					*(big_data + il*blowup+i2b+offset_xb + jl*blowup*nxb + offset_yb*nxb ) = val;
					val += step;
					}
				}
			/* Fill in the last center value on the right, which was left unfilled by the above alg */
#ifdef CHECK_MEM
			if( (nxl-1)*blowup+offset_xb + jl*blowup*nxb + offset_yb*nxb >= array_size ) { fprintf( stderr, "mem error 004\n" ); exit(-1); }
#endif
			*(big_data + (nxl-1)*blowup+offset_xb + jl*blowup*nxb + offset_yb*nxb ) = v->data[(nxl-1) + jl*nxl];
			}

		/* Vertical base lines */
		for( jl=0; jl<(long)nyl-1; jl++ ) 
		for( il=0; il<(long)nxl;   il++ ) {
			base_val  = v->data[il + jl*nxl];
			below_val = v->data[il + (jl+1)*nxl];

			miss_base  = close_enough(base_val,  fill_val);
			miss_below = close_enough(below_val, fill_val);

			if( miss_base ) {
				if( miss_below ) {
					/* BOTH missing */
					step = 0.0;
					val = base_val;		/* missing value */
					}
				else
					{
					/* base missing, but below is there */
					step = 0.0;
					val = below_val;	/* an OK value */
					}
				}
			else if( miss_below ) {
				/* ONLY below is missing, checked for both missing above */
				val = base_val;
				step = 0.0;
				}
			else
				{
				/* NEITHER missing */
				step = (below_val-base_val)*bupr;
				val = base_val;
				}

			for( j2b=0; j2b < blowup; j2b++ ) {
#ifdef CHECK_MEM
			if( il*blowup+offset_xb + jl*blowup*nxb + (j2b+offset_yb)*nxb >= array_size ) { fprintf( stderr, "mem error 005\n" ); exit(-1); }
#endif
				*(big_data + il*blowup+offset_xb + jl*blowup*nxb + (j2b+offset_yb)*nxb ) = val;
				val += step;
				}
			}
		/* Fill in the last center value along the top, which was left unfilled by the above alg */
		for( il=0; il<(long)nxl; il++ ) {
#ifdef CHECK_MEM
			if( il*blowup+offset_xb + (nyl-1)*blowup*nxb + offset_yb*nxb >= array_size ) { fprintf( stderr, "mem error 006\n" ); exit(-1); }
#endif
			*(big_data + il*blowup+offset_xb + (nyl-1)*blowup*nxb + offset_yb*nxb) = v->data[il + (nyl-1)*nxl];
			}

		/* Now, fill in the interior of the interior squares by 
		 * interpolating from the horizontal and vertical
		 * base lines.
		 */
		for( jl=0; jl<(long)nyl-1; jl++ )
		for( il=0; il<(long)nxl-1; il++ ) {
			for( j2b=1; j2b<blowup; j2b++ )
			for( i2b=1; i2b<blowup; i2b++ ) {
				frac_x = (float)i2b*bupr;
				frac_y = (float)j2b*bupr;

				base_x    = *(big_data +  il   *blowup+offset_xb + jl*blowup*nxb +(j2b+offset_yb)*nxb);
				right_val = *(big_data + (il+1)*blowup+offset_xb + jl*blowup*nxb+ (j2b+offset_yb)*nxb);
				base_y    = *(big_data + il*blowup+i2b+offset_xb +  jl   *blowup*nxb + offset_yb*nxb);
				below_val = *(big_data + il*blowup+i2b+offset_xb + (jl+1)*blowup*nxb + offset_yb*nxb);

				if( close_enough(base_x,    fill_val) || 
				    close_enough(right_val, fill_val) || 
				    (il == (long)nxl-1) )
					del_x = 0.0;
				else
					del_x  = right_val - base_x;
				if( close_enough(base_y,    fill_val) || 
				    close_enough(below_val, fill_val) || 
				    (jl == (long)nyl-1) )
					del_y = 0.0;
				else
					del_y  = below_val - base_y;
				est1 = frac_x*del_x + base_x;
				est2 = frac_y*del_y + base_y;

				if( close_enough( est1, fill_val )) {
					if( close_enough( est2, fill_val ))
						final_est = fill_val;
					else
						final_est = est2;
					}
				else if( close_enough( est2, fill_val ))
					final_est = est1;
				else
					final_est = (est1 + est2)*.5;

#ifdef CHECK_MEM
				if( il*blowup+i2b+offset_xb + jl*blowup*nxb + (j2b+offset_yb)*nxb >= array_size ) { fprintf( stderr, "mem error 007\n" ); exit(-1); }
#endif
				*(big_data + il*blowup+i2b+offset_xb + jl*blowup*nxb + (j2b+offset_yb)*nxb ) = final_est;
				}
			}

		/* It is a tricky and undetermined question as to whether we want to allow
		 * extrema on the boundaries.  As a complete and total hack, we use only 
		 * some fraction of the linear projection when extrapolating out to the 
		 * edges.  If this is set to 1, then full linear extrapolation is used;
		 * if set to 0, no extrapolation is done.
		 */
		extrap_fact = 0.2;

		/* Fill in right hand side by extrapolating the gradient from the interior square fill.
		 * This goes from y=the first center point to y=the last center point.
		 */
		il = nxl-1;
		for( j2b=0; j2b<=blowup*(long)(nyl-1); j2b++ ) {
			idx = il*blowup+offset_xb + (j2b+offset_yb)*nxb;	
			step = (*(big_data + idx - 1) - *(big_data + idx - 2));
			val  = *(big_data + idx) + step;
			for( i2b=1; i2b<(blowup-offset_xb+1); i2b++ ) {
#ifdef CHECK_MEM
				if( idx + i2b >= array_size ) { fprintf( stderr, "mem error 008\n" ); exit(-1); }
#endif
				*(big_data + idx + i2b) = val;
				val += step*extrap_fact;
				}
			}

		/* Fill in left hand side */
		il = 0;
		for( j2b=0; j2b<=blowup*(long)(nyl-1); j2b++ ) {
			idx = il*blowup+offset_xb + (j2b+offset_yb)*nxb;
			step = (*(big_data + idx + 2) - *(big_data + idx + 1));
			val  = *(big_data + idx) - step;
			for( i2b=1; i2b<=(blowup-1)/2; i2b++ ) {
#ifdef CHECK_MEM
				if( idx - i2b >= array_size ) { fprintf( stderr, "mem error 009\n" ); exit(-1); }
#endif
				*(big_data + idx - i2b) = val;
				val -= step*extrap_fact;
				}
			}

		/* Fill in bottom */
		jl = 0;
		for( i2b=0; i2b<=blowup*(long)(nxl-1); i2b++ ) {
			idx = i2b+offset_xb + jl*blowup*nxb + offset_yb*nxb;
			step = (*(big_data + idx + 2*nxb) - *(big_data + idx + nxb));   /* big(,y+2) - big(,y+1) */
			val  = *(big_data + idx) - step;
			for( j2b=1; j2b<=(blowup-1)/2; j2b++ ) {
#ifdef CHECK_MEM
				if( idx - j2b*nxb >= array_size ) { fprintf( stderr, "mem error 010\n" ); exit(-1); }
#endif
				*(big_data + idx - j2b*nxb) = val;
				val -= step*extrap_fact;
				}
			}

		/* Fill in top */
		jl = nyl-1;
		for( i2b=0; i2b<blowup*(long)(nxl-1); i2b++ ) {
			idx = i2b+offset_xb + jl*blowup*nxb + offset_yb*nxb;
			step = (*(big_data + idx - nxb) - *(big_data + idx - 2*nxb));  /* big(,y-1) - big(,y-2) */
			val  = *(big_data + idx) + step;
			for( j2b=1; j2b<=blowup/2; j2b++ ) {
#ifdef CHECK_MEM
				if( idx + j2b*nxb >= array_size ) { fprintf( stderr, "mem error 011\n" ); exit(-1); }
#endif
				*(big_data + idx + j2b*nxb) = val;
				val += step*extrap_fact;
				}
			}

		/* Still have to fill in the four corners at this point.   Because of the
		 * extrapolation issue noted above, we take a simple approach.  Just fill
		 * in the corner blocks with the center data value.
		 */

		/* Lower left corner */
		il = 0;
		jl = 0;
		cval = v->data[il + jl*nxl];          /* Data value in lower left corner */
		if( ! close_enough( cval, fill_val )) {
			/* Fill in lower left corner */
			for( j2b=0; j2b<=offset_yb; j2b++ )
			for( i2b=0; i2b<=offset_xb; i2b++ ) {
#ifdef CHECK_MEM
				if( i2b + j2b*nxb >= array_size ) { fprintf( stderr, "mem error 012\n" ); exit(-1); }
#endif
				*(big_data + i2b + j2b*nxb) = cval;
				}
			}
			
		/* Lower right corner */
		il = nxl - 1;
		jl = 0;
		cval = v->data[il + jl*nxl];          /* Data value in lower left corner */
		if( ! close_enough( cval, fill_val )) {
			/* Fill in lower right corner */
			for( j2b=0; j2b<=offset_yb; j2b++ )
			for( i2b=offset_xb; i2b<blowup; i2b++ ) {
#ifdef CHECK_MEM
				if( il*blowup + i2b + j2b*nxb >= array_size ) { fprintf( stderr, "mem error 013\n" ); exit(-1); }
#endif
				*(big_data + il*blowup + i2b + j2b*nxb) = cval;
				}
			}

		/* Upper right corner */
		il = nxl - 1;
		jl = nyl - 1;
		cval = v->data[il + jl*nxl];          /* Data value in lower left corner */
		if( ! close_enough( cval, fill_val )) {
			/* Fill in upper right corner */
			for( j2b=offset_yb; j2b<blowup; j2b++ )
			for( i2b=offset_xb; i2b<blowup; i2b++ ) {
#ifdef CHECK_MEM
				if( il*blowup + i2b + jl*blowup*nxb + j2b*nxb >= array_size ) { fprintf( stderr, "mem error 014\n" ); exit(-1); }
#endif
				*(big_data + il*blowup + i2b + jl*blowup*nxb + j2b*nxb) = cval;
				}
			}

		/* Upper left corner */
		il = 0;
		jl = nyl - 1;
		cval = v->data[il + jl*nxl];          /* Data value in lower left corner */
		if( ! close_enough( cval, fill_val )) {
			/* Fill in upper left corner */
			for( j2b=offset_yb; j2b<blowup; j2b++ )
			for( i2b=0; i2b<=offset_xb; i2b++ ) {
#ifdef CHECK_MEM
				if(  il*blowup + i2b + jl*blowup*nxb + j2b*nxb >= array_size ) { fprintf( stderr, "mem error 015\n" ); exit(-1); }
#endif
				*(big_data + il*blowup + i2b + jl*blowup*nxb + j2b*nxb) = cval;
				}
			}

		/* Paint missing value blocks */
		for( jl=0; jl<(long)nyl; jl++ )
		for( il=0; il<(long)nxl; il++ ) {
			base_val  = v->data[il   + jl*nxl];
			if( close_enough( base_val, fill_val )) {
				for( j2b=0; j2b<blowup; j2b++ )
				for( i2b=0; i2b<blowup; i2b++ ) {
#ifdef CHECK_MEM
					if( il*blowup+i2b + jl*nxb*blowup + j2b*nxb >= array_size ) { fprintf( stderr, "mem error 016\n" ); exit(-1); }
#endif
					*(big_data + il*blowup+i2b + jl*nxb*blowup + j2b*nxb ) = base_val;
					}
				}
			}

		}	/* end of BlowupType::Bilinear case */
}

/******************************************************************************
 * Set the style of blowup we want to do.
 */
	void
set_blowup_type( BlowupType new_type )
{
	if( new_type == BlowupType::Replicate ) 
		in_set_label( Label::BlowupType, "Repl"   );
	else
		in_set_label( Label::BlowupType, "Bi-lin" );

	options.blowup_type = new_type;
}

/******************************************************************************
 * If we allowed strings of arbitrary length, some of the widgets
 * would crash when trying to display them.
 */
	/* Was: char *limit_string(char *s), trimming trailing spaces and
	 * truncating by writing '\0' bytes into the CALLER's own buffer and
	 * returning that same pointer. Every call site (all in view.cc)
	 * only ever uses the return value inline in a snprintf(), never
	 * re-reads the original buffer afterward expecting it pre-trimmed --
	 * so this is now a pure function computing into a fresh std::string,
	 * with no more side effect on the caller's storage. */
	std::string
limit_string( std::string_view s )
{
	std::string ret( s );

	int	i = (int)ret.size() - 1;
	while( i >= 0 && ret[i] == ' ' )
		i--;
	ret.resize( i+1 );

	if( ret.size() > MAX_DISPLAYED_STRING_LENGTH )
		ret.resize( MAX_DISPLAYED_STRING_LENGTH );

	return( ret );
}

/******************************************************************************
 * If we try to print to an already existing file, then warn the user
 * before clobbering it.
 */
	Message
warn_if_file_exits( char *fname )
{
	Message	retval;
	FILE	*f;
	char	message[1024];

	if( (f = fopen(fname, "r")) == NULL )
		return( Message::OK );
	fclose(f);

	snprintf( message, 1022, "OK to overwrite existing file %s?\n", fname );
	retval = in_dialog( message, true );
	return( retval );
}

/******************************************************************************/

	static void
handle_time_dim( int fileid, NCVar *v, int dimid )
{
	NCDim   *d;

	d = v->dim[dimid].get();

	if( udu_utistime( const_cast<char *>(d->name.c_str()), const_cast<char *>(d->units.c_str()) ) ) {
		d->timelike = 1;
		d->time_std = TimeStandard::Udunits;
		d->tgran    = udu_calc_tgran( fileid, v, dimid );
		}
	else if( epic_istime0( fileid, v, d )) {
		d->timelike = 1;
		d->time_std = TimeStandard::Epic0;
		d->tgran    = epic_calc_tgran( fileid, d );
		}
	else if( (!d->units.empty()) &&
		 (d->units.size() >= 5) &&
		 (strncasecmp( d->units.c_str(), "month", 5 ) == 0 ))  {
		d->timelike = 1;
		d->time_std = TimeStandard::Months;
		d->tgran    = months_calc_tgran( fileid, d );
		}
	else
		d->timelike = 0;
}

/******************************************************************************/
	static TimeGranularity
months_calc_tgran( int fileid, NCDim *d )
{
	char	temp_string[128];
	float	delta, v0, v1;
	int	type, has_bounds;
	double	temp_double, bounds_min, bounds_max;

	if( d->size < 2 ) {
		return( TimeGranularity::Day );
		}

	type = netcdf_dim_value( fileid, const_cast<char *>(d->name.c_str()), 0L, &temp_double, temp_string, 0L, &has_bounds, &bounds_min, &bounds_max );
	if( type == NC_DOUBLE )
		v0 = (float)temp_double;
	else
		{
		fprintf( stderr, "Note: can't calculate time granularity, unrecognized timevar type (%d)\n",
			type );
		return( TimeGranularity::Day );
		}

	type = netcdf_dim_value( fileid, const_cast<char *>(d->name.c_str()), 1L, &temp_double, temp_string, 1L, &has_bounds, &bounds_min, &bounds_max );
	if( type == NC_DOUBLE )
		v1 = (float)temp_double;
	else
		{
		fprintf( stderr, "Note: can't calculate time granularity, unrecognized timevar type (%d)\n",
			type );
		return( TimeGranularity::Day );
		}
	
	delta = v1 - v0;

	if( delta > 11.5 ) 
		return( TimeGranularity::Year );
	if( delta > .95 ) 
		return( TimeGranularity::Month );
	if( delta > .03 ) 
		return( TimeGranularity::Day );

	return( TimeGranularity::Min );
}

/******************************************************************************/
void fmt_time( char *temp_string, size_t temp_string_len, double new_dimval, NCDim *dim, int include_granularity )
{
	int 	year, month, day;

	if( ! dim->timelike ) {
		fprintf( stderr, "ncview: internal error: fmt_time called on non-timelike axis!\n");
		fprintf( stderr, "dim name: %s\n", dim->name.c_str() );
		exit( -1 );
		}

	if( dim->time_std == TimeStandard::Udunits ) 
		udu_fmt_time( temp_string, temp_string_len, new_dimval, dim, include_granularity );

	else if( dim->time_std == TimeStandard::Epic0 ) 
		epic_fmt_time( temp_string, temp_string_len, new_dimval, dim );

	else if( dim->time_std == TimeStandard::Months ) {
		/* Format for months standard */
		year  = (int)( (new_dimval-1.0) / 12.0 );
		month = (int)( (new_dimval-1.0) - year*12 + .01 );
		month = (month < 0) ? 0 : month;
		month = (month > 11) ? 11 : month;
		day   =
		   (int)( ((new_dimval-1.0) - year*12 - month) * 30.0) + 1;
		snprintf( temp_string, temp_string_len-1, "%s %2d %4d", month_name[month],
				day, year+1 );
		}

	else
		{
		fprintf( stderr, "Internal error: uncaught value of tim_std=%d\n", static_cast<int>(dim->time_std) );
		exit( -1 );
		}
}

/*********************************************************************************************
 * like strncmp, but ignoring case
 */
	int
strncmp_nocase( const char *s1, const char *s2, size_t n )
{
	size_t	i;
	int	retval;

	if( (s1==NULL) || (s2==NULL))
		return(-1);

	std::vector<char> s1_lc_buf(strlen(s1)+1), s2_lc_buf(strlen(s2)+1);
	char *s1_lc = s1_lc_buf.data();
	char *s2_lc = s2_lc_buf.data();

	for( i=0; i<strlen(s1); i++ )
		s1_lc[i] = tolower(s1[i]);
	s1_lc[i] = '\0';
	for( i=0; i<strlen(s2); i++ )
		s2_lc[i] = tolower(s2[i]);
	s2_lc[i] = '\0';

	retval = strncmp( s1_lc, s2_lc, n );

	return(retval);
}

/**************************************************************************************************
 * Determine if the passed string names a lat or if the string names a lon.
 * If we figure out either lat or lon, returns 0 (success).
 * If we cannot figure either lat or lon, returns 1 (error).
 */
int determine_lat_lon( char *s_in, int *is_lat, int *is_lon )
{
	static  int have_given_warning = 0;
	size_t	n, i;

	/* Get lower case version of input name */
	n = strlen(s_in);
	std::vector<char> s_buf( n+2 );
	char *s = s_buf.data();

	for( i=0; i<n; i++ )
		s[i] = tolower( s_in[i] );

	*is_lat = 0;
	*is_lon = 0;

	if( strncasecmp( "lat", s, 3 ) == 0 ) {
		*is_lat = 1;
		return(0);
		}

	if( strncasecmp( "lon", s, 3 ) == 0 ) {
		*is_lon = 1;
		return(0);
		}

	if( strstr( s, "lat" ) != NULL ) {
		*is_lat = 1;
		return(0);
		}

	if( strstr( s, "lon" ) != NULL ) {
		*is_lon = 1;
		return(0);
		}

	if( (s[0] == 'x') || (s[0] == 'X') ) {
		*is_lon = 1;
		return(0);
		}

	if( (s[0] == 'y') || (s[0] == 'Y') ) {
		*is_lat = 1;
		return(0);
		}

	if( (have_given_warning == 0) && options.debug ) {
		have_given_warning = 1;
		fprintf( stderr, "Warning, cannot figure out whether coordinate variable \"%s\" is a latitude or a longitude, just based on its name\n", s_in );
		fprintf( stderr, "Please name it either Latitude or Longitude, as appropriate, or send email to dpierce@ucsd.edu if you have a case that does not fit this description so I can fix it.\n----------------\n" );
		}

	return(1);	/* error return */
}

/*******************************************************************************************
 * Returns the number of forward slashes in a string
 */
int count_nslashes( const char *s ) 
{
	size_t	i;
	int	nslash;

	nslash = 0;
	for( i=0; i<strlen(s); i++ ) 
		if( s[i] == '/' )
			nslash++;

	return( nslash );
}

/*******************************************************************************************
 * Given a list of variables, this returns a stringlist of unique group names. If ANY var
 * lives in the root group, then the return list includes "/". If no var lives in the root
 * group, then the list does NOT include "/".
 */
Stringlist *get_group_list( const std::vector<std::unique_ptr<NCVar>> &vars )
{
	Stringlist	*retval = NULL;
	char		group_name[ MAX_NC_NAME*20 ];	/* Assume no more than 20 levels of groups */

	for( const auto &cursor : vars ) {

		unpack_groupname( cursor->name.c_str(), -1, group_name );	/* -1 means get full group name */

		/* Only add to list if not already there */
		if( stringlist_match_string_exact( retval, group_name ) == nullptr )
			stringlist_add_string( &retval, group_name );
		}

	return( retval );
}

/*******************************************************************************************
 * Given a varname string of format: groupname0/groupname1/groupnameN/varname
 *
 * and an integer ig: 0...N this returns groupname correspoinding to the integer ig
 * (NOTE: counting starts at 0, so if ig==0 then the first group name is returned)
 *
 * If ig == -1, then the full groupname without the varname is returned:
 * I.e., "groupname0/groupname1/groupnameN". If the var does NOT have any
 * forward slashes, it lives in the root group, and "/" is returned.
 *
 * If ig == -2, then ONLY the varname is returned. I.e., "varname"
 *
 * groupname must already be allocated upon entry
 *
 * Returns 0 on success, -1 on error
 */
int unpack_groupname( const char *varname, int ig, char *groupname ) 
{
	size_t	i;
	int	i0, i1, idx_slash[MAX_NC_NAME], nslash;
	char	ts[MAX_NC_NAME];

	/* Get indices of the slashes */
	nslash = 0;
	for( i=0; i<strlen(varname); i++ ) {
		if( varname[i] == '/' ) {
			idx_slash[nslash] = i;
			nslash++;
			}
		}

	if( nslash == 0 ) {
		if (ig == -2 ) {
			/* Asked for varname only */
			snprintf( groupname, MAX_NC_NAME, "%s", varname );
			return(0);
			}
		else
			{
			/* If no slashes in the var name, must live in root group */
			snprintf( groupname, MAX_NC_NAME, "%s", "/" );
			return( 0 );
			}
		}

	if( ig > (nslash+1) ) {
		fprintf( stderr, "Error in unpack_groupname: varname: >%s< group to find (starting at 0)=%d invalid group to find (not this many groups in the varname)\n",
			varname, ig );
		exit(-1);
		}

	snprintf( ts, sizeof(ts), "%s", varname );

	if( ig == -2 ) {
		snprintf( groupname, MAX_NC_NAME, "%s", ts+idx_slash[nslash-1]+1 );
		return( 0 );
		}

	if( ig == -1 ) {
		ts[ idx_slash[nslash-1] ] = '\0';
		snprintf( groupname, MAX_NC_NAME, "%s", ts );
		return( 0 );
		}

	if( ig == 0 ) 
		i0 = 0;
	else
		i0 = idx_slash[ig-1] + 1;
	i1 = idx_slash[ig];
	ts[i1] = '\0';

	snprintf( groupname, MAX_NC_NAME, "%s", ts+i0 );

	return( 0 );
}

/*******************************************************************************************
 * Given a varname string of format: groupname0/groupname1/groupnameN/varname
 * this returns ONLY the trailing varname in "varname_sans_groups", and ONLY the
 * groupname with no leading or trailing slash ( "root/groupa" ) in "groupname"
 */
void varname_no_groups( const char *varname, char *varname_sans_groups, char *groupname )
{
	size_t	i;
	int	idx_slash[MAX_NC_NAME], nslash;

	/* Get indices of the slashes */
	nslash = 0;
	for( i=0; i<strlen(varname); i++ ) {
		if( varname[i] == '/' ) {
			idx_slash[nslash] = i;
			nslash++;
			}
		}

	if( nslash == 0 ) {
		snprintf( varname_sans_groups, MAX_NC_NAME, "%s", varname );
		if( groupname != NULL )
			groupname[0] = '\0';
		return;
		}

	snprintf( varname_sans_groups, MAX_NC_NAME, "%s", varname+idx_slash[nslash-1]+1 );
	if( groupname != NULL ) {
		strncpy( groupname, varname, idx_slash[nslash-1] );
		groupname[ idx_slash[nslash-1] ] = '\0';
		}

	/*
	printf( "UUUU varname_no_groups, varname: >%s< varname_sans_groups: >%s< groupname: >%s<\n",
		varname,
		varname_sans_groups,
		((groupname == NULL) ? "NULL" : groupname));
	*/
}

/*****************************************************************************
 * Indicate an error condition which can be continued from. Routed through
 * in_dialog (a real ncview_ui responsibility) rather than being one itself.
 * Formerly interface_glue.cc, dissolved into this file since core's other
 * error-reporting callers already live here.
 */
void
in_error( const char *message )
{
	in_dialog( message, false );
}


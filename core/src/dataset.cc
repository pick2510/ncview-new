/*
 * core/src/dataset.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * See ncview/dataset.h. OOP_redesign plan, Step 5.
 */
#include "ncview/dataset.h"

#include "ncview/includes.h"
#include "ncview/protos.h"	/* fi_close() */

#ifdef HAVE_UDUNITS2
#include "udunits2.h"
extern ut_system *unitsys;
#endif

extern Options options;

void NetCDFFile::close()
{
	if( fileid_ >= 0 ) {
		fi_close( fileid_ );
		fileid_ = -1;
		}
}

NetCDFFile::~NetCDFFile()
{
	close();
}

NetCDFFile *Dataset::trackFile( int fileid )
{
	for( auto &f : files_ )
		if( f->id() == fileid )
			return f.get();

	files_.push_back( std::make_unique<NetCDFFile>( fileid ) );
	return files_.back().get();
}

int FDBlist::id() const
{
	return file ? file->id() : 0;
}

namespace {

/* Formerly util.cc's new_fdblist(): allocate a new FDBlist element with
 * its NetCDFOptions aux_data slot initialized. Only used by
 * Dataset::addVariable(). */
std::unique_ptr<FDBlist> new_fdblist()
{
	auto el = std::make_unique<FDBlist>();
	NetCDFOptions *new_netcdf_options;

	el->filename = "UNINITIALIZED";

#ifdef HAVE_UDUNITS2
	el->ut_unit_ptr = NULL;
#endif

	new_netcdf( &new_netcdf_options );
	el->aux_data.reset( new_netcdf_options );

	return( el );
}

/* Formerly util.cc's equivalent_FDBs(): true if v1 and v2 live in exactly
 * the same sequence of files. Only used by
 * Dataset::copyInfoToIdenticalDims(). */
int equivalent_FDBs( NCVar *v1, NCVar *v2 )
{
	if( v1->files.size() != v2->files.size() )
		return(0); /* files differ */

	for( size_t i=0; i<v1->files.size(); i++ )
		if( v1->files[i]->id() != v2->files[i]->id() )
			return(0); /* files differ */

	return(1);
}

} // namespace

NCVar *Dataset::findVariable( const char *var_name )
{
	for( auto &vptr : variables_ )
		if( vptr->name == var_name )
			return( vptr.get() );

	return( NULL );
}

void Dataset::addVariables( Stringlist *var_list, int id, const char *filename, int nfiles )
{
	if( options.debug )
		printf( "add_vars_to_list: entering, adding vars to list for file %s\n", filename );
	if( var_list != NULL )
	for( auto &e : *var_list ) {
		if( options.debug )
			printf( "adding variable %s to list\n", e.string.c_str() );
		addVariable( e.string.c_str(), id, filename, nfiles );
		}
	if( options.debug )
		printf( "done adding vars for file %s\n", filename );
}

void Dataset::addVariable( const char *var_name, int file_id, const char *filename, int nfiles )
{
	NCVar	*var;
	int	n_dims;

	/* make a new file description entry for this var/file combo */
	auto new_fdb_owner = new_fdblist();
	FDBlist *new_fdb = new_fdb_owner.get();
	new_fdb->file     = trackFile( file_id );
	{
	size_t *raw_size = fi_var_size( file_id, const_cast<char *>(var_name) );
	int raw_n_dims = fi_n_dims( file_id, const_cast<char *>(var_name) );
	new_fdb->var_size.assign( raw_size, raw_size + raw_n_dims );
	free( raw_size );
	}
	if( strlen(filename) > (MAX_FILE_NAME_LEN-1)) {
		fprintf( stderr, "Error, input file name is too long; longest I can handle is %d\nError occurred on file %s\n",
			MAX_FILE_NAME_LEN, filename );
		exit(-1);
		}
	new_fdb->filename = filename;

	/* fill out auxiliary (data-file format dependent) information
	 * for the new fdb.
	 */
	fi_fill_aux_data( file_id, const_cast<char *>(var_name), new_fdb );
#ifdef HAVE_UDUNITS2
	new_fdb->ut_unit_ptr = ut_parse( unitsys, new_fdb->recdim_units.c_str(), UT_ASCII ); /* Will be NULL if there was an error */
#endif

	/* Does this variable already have an entry on the list? */
	var = findVariable( var_name );
	if( var == NULL ) {	/* NO -- make a new NCVar structure */
		auto new_var_owner = std::make_unique<NCVar>();
		NCVar *new_var = new_var_owner.get();
		new_var->name       = var_name;
		n_dims              = fi_n_dims( file_id, const_cast<char *>(var_name) );
		new_var->n_dims     = n_dims;
		if( options.debug )
			printf( "adding variable %s with %d dimensions\n",
				var_name, n_dims );
		new_var->global_min = 0.0;
		new_var->global_max = 0.0;
		new_var->user_min   = 0.0;
		new_var->user_max   = 0.0;
		new_var->user_set_blowup   = -99999;
		new_var->auto_set_no_range = 0;
		new_var->have_set_range    = false;
		{
		size_t *raw_size = fi_var_size( file_id, const_cast<char *>(var_name) );
		new_var->size.assign( raw_size, raw_size + n_dims );
		free( raw_size );
		}
		new_fdb->index      = 0;	/* Since this is the FIRST fdb for this var */
		new_var->files.push_back( std::move( new_fdb_owner ) );
		new_var->fill_value = DEFAULT_FILL_VALUE;
		fi_fill_value( new_var, &(new_var->fill_value) );

		/* Init the dim mapping info -- scalar_dim_map_info starts empty
		 * and grows (up to MAX_SCALAR_COORDS) as scalar coords are
		 * discovered in handle_dim_mapping_scalar(). */
		handle_dim_mapping( new_var );	/* needs to be before fill_dim_structs cuz latter access fi_dim_info */

		fill_dim_structs( new_var );
		new_var->is_virtual = false;
		variables_.push_back( std::move( new_var_owner ) );

		}
	else	/* YES -- just add the FDB to the list of files in which
		 * this variable appears, and accumulate the variable's size.
		 */
		{
		/* Go to the end of the file list and add it there */
		if( options.debug )
			printf( "adding another file with variable %s in it\n",
				var_name );
		if( var->files.empty() ) {
			fprintf( stderr, "ncview: add_var_to_list: internal ");
			fprintf( stderr, "inconsistency; var has no last_file\n" );
			exit( -1 );
			}
		new_fdb->index    = var->files.back()->index + 1;	/* so index for this fdb is 1 more than index for prev one */
		var->size[0]      += new_fdb->var_size[0];	/* this works b/c you can only concatenate across first (timelike) dim */
		/* var->dim[0] (if scannable) is the NCDim fill_dim_structs() built
		 * from the FIRST file alone, back when this var was created above --
		 * its ->size field is a separate copy of what var->size[0] was at
		 * that time, not a view onto it, so it silently goes stale here
		 * unless kept in sync too. Left stale, this is invisible almost
		 * everywhere else (view_change_cur_dim()'s prev/next stepping and
		 * set_scan_view()'s "frame N/M" label both read var->size[0]
		 * directly), but MainWindow::fillDimInfo() sets the scan-axis
		 * slider's range from exactly this ->size -- so for a multi-file
		 * (per-timestep-per-file, e.g. WRF-style) virtual variable, the
		 * slider silently gets stuck at whatever range the first file
		 * alone had (bounds(0,0), i.e. no visible range, if each file only
		 * contributes one timestep) while every other UI element already
		 * reflects the full concatenated size.
		 */
		if( var->dim[0] != nullptr )
			var->dim[0]->size = var->size[0];
		var->files.push_back( std::move( new_fdb_owner ) );
		var->is_virtual   = true;
		}
}

void Dataset::cacheScalarCoordInfo()
{
	NCDim_map_info	*dmi;
	float		fval;
	size_t		zeros[MAX_NC_DIMS], ones[MAX_NC_DIMS], n_ts, ii, i_cursor, n_ts_this_file;

	if( options.debug ) printf( "cache_scalar_coord_info: entering\n" );

	/* Allocate space for the timestep_2_fdb array. This points
	 * to the file (FDBlist) associated with EACH TIMESTEP of
	 * the variable
	 */
	for( const auto &vptr : variables_ ) {
		NCVar *v = vptr.get();
		n_ts = v->size[0];	/* total number of timesteps across ALL files */
		if( n_ts > 0 ) {
			if( options.debug )
				printf( "Constructing timestep_2_fdb array for var %s, which has %zu timesteps\n", v->name.c_str(), n_ts );
			/* One FDBlist pointer for each timestep of the var */
			v->timestep_2_fdb.resize( n_ts );

			i_cursor = 0L;
			for( auto &tfile_owner : v->files ) {
				FDBlist *tfile = tfile_owner.get();
				/* Set all FDBpointers for the timesteps in THIS file
				 * to point to this file
				 */
				n_ts_this_file = tfile->var_size[0];
				if( options.debug )
					printf( "%zu timesteps of var %s are in file %s\n", n_ts_this_file, v->name.c_str(), tfile->filename.c_str() );
				for( ii=0; ii<n_ts_this_file; ii++ )
					v->timestep_2_fdb[i_cursor++] = tfile;
				}
			if( i_cursor != n_ts ) {
				fprintf( stderr, "Internal error: in routine cache_scalar_coord_info, got a total length of the unlimited dim in var %s to be %zu, but when setting pointers to the files, there seemd to be only %zu entries\n",
					v->name.c_str(), n_ts, i_cursor );
				exit(-1);
				}
			}
		}

	/* These will be used as the start (zeros) and count (ones)
	 * to get the scalar data
	 */
	for( int isc=0; isc<MAX_NC_DIMS; isc++ ) {
		zeros[isc] = 0L;
		ones[isc]  = 1L;
		}

	for( const auto &vptr : variables_ ) {
		NCVar *v = vptr.get();
		int nsc = (int)v->scalar_dim_map_info.size();
		if( nsc > 0 ) {
			/* How many files does this var live in? */
			int nfiles = (int)v->files.size();
			if( options.debug )
				printf( "Making cache for the %d SCALAR coordinates of variable %s, which lives in %d files\n", nsc, v->name.c_str(), nfiles );

			/* We hold the values of the scalar coords in the data_cache */
			for( int isc=0; isc<nsc; isc++ ) {
				dmi = v->scalar_dim_map_info[isc].get();
				dmi->data_cache.resize( nfiles ); /* one val per FILE (not timestep) */
				}

			/* Go through each file and read in the vals of all the scalar coords */
			for( int ifile=0; ifile<nfiles; ifile++ ) {
				FDBlist *tfile = v->files[ifile].get();
				for( int isc=0; isc<nsc; isc++ ) {
					dmi = v->scalar_dim_map_info[isc].get();
					if( dmi == NULL ) {
						fprintf( stderr, "Coding error, uninitialized pointer to a scalar dim info struct is being used\n" );
						exit(-1);
						}
					netcdf_fi_get_data( tfile->id(), const_cast<char *>(dmi->coord_var_name.c_str()), zeros, ones, &fval, NULL );
					if( options.debug ) printf( "In file %d/%d, value of scalar coord \"%s\" is %f %s\n",
						ifile, nfiles, dmi->coord_var_name.c_str(), fval, dmi->coord_var_units.c_str() );
					dmi->data_cache[ifile] = fval;
					}
				}

			/* Now see if all the scalar values are the same */
			for( int isc=0; isc<nsc; isc++ ) {
				dmi = v->scalar_dim_map_info[isc].get();
				dmi->scalar_all_same = 1;
				if( nfiles > 1 ) {
					for( int ifile=1; ifile<nfiles; ifile++ ) {
						if( dmi->data_cache[ifile] != dmi->data_cache[0] )
							dmi->scalar_all_same = 0;
						}
					}
				}
			}
		}

	if( options.debug ) printf( "cache_scalar_coord_info: finished\n" );
}

void Dataset::copyInfoToIdenticalDims( NCVar *vsrc, NCDim *dsrc, size_t dim_len )
{
	int	i, dims_are_same;
	NCDim	*d;

	for( auto &vptr : variables_ ) {
		NCVar *v = vptr.get();
		for( i=0; i<v->n_dims; i++ ) {
			d = v->dim[i].get();
			if( (d != NULL) && (d->have_calc_minmax == 0)) {
				/* See if this dim is same as passed dim */
				dims_are_same = (dsrc->name == d->name) &&
						equivalent_FDBs( vsrc, v );
				if( dims_are_same ) {
					if( options.debug ) {
						printf( "Dim %s (%d) is same as dim %s (%d), copying min&max from former to latter... min=%f max=%f\n",
							dsrc->name.c_str(), dsrc->global_id, d->name.c_str(), d->global_id,
							dsrc->min, dsrc->max );
						}
					d->min = dsrc->min;
					d->max = dsrc->max;
					d->have_calc_minmax = 1;
					d->values.assign( dsrc->values.begin(), dsrc->values.begin() + dim_len );
					d->is_lat = dsrc->is_lat;
					d->is_lon = dsrc->is_lon;
					}
				}
			}
		}
}

void Dataset::calcDimMinmaxes()
{
	int	i, j;
	NCDim	*d;
	char	temp_str[1024];
	nc_type	type;
	double	temp_double, bounds_max, bounds_min;
	int	has_bounds, name_lat, name_lon, units_lat, units_lon;
	size_t	dim_len;
	size_t	cursor_place[MAX_NC_DIMS];

	for( auto &vptr : variables_ ) {
		NCVar *v = vptr.get();
		for( i=0; i<v->n_dims; i++ ) {
			d = v->dim[i].get();
			if( (d != NULL) && (d->have_calc_minmax == 0)) {
				if( options.debug )
					printf( "%s %d ...min & maxes for dim d->name=>%s< (d->global_id=%d)...\n",
						__FILE__, __LINE__, d->name.c_str(), d->global_id );
				dim_len = v->size[i];
				d->values.resize( dim_len );

				for( j=0; j<v->n_dims; j++ )
					cursor_place[j] = (int)(v->size[j]/2.0);	/* take middle in case 2-d mapped dims apply */

				type = fi_dim_value( v, i, 0L, &temp_double, temp_str, &has_bounds, &bounds_min,
								&bounds_max, cursor_place );	/* used to get type ONLY */
				if( type == NC_DOUBLE ) {
					for( j=0; j<(int)dim_len; j++ ) {
						cursor_place[i] = j;
						type = fi_dim_value( v, i, j, &temp_double, temp_str, &has_bounds, &bounds_min, &bounds_max, cursor_place );
						d->values[j] = (float)temp_double;
						}
					d->min  = d->values[0];
					d->max  = d->values[dim_len - 1];
					}
				else
					{
					if( options.debug )
						printf( "**Note: non-float dim found; i=%d\n", i );
					d->min  = 1.0;
					d->max  = (float)dim_len;
					for( j=0; j<(int)dim_len; j++ )
						d->values[j] = (float)j;
					}
				d->have_calc_minmax = 1;

				/* Try to see if the dim is a lat or lon.  Not an exact science by a long shot */
				name_lat  = strncmp_nocase(d->name.c_str(),  "lat",    3)==0;
				units_lat = strncmp_nocase(d->units.c_str(), "degree", 6) == 0;
				name_lon  = strncmp_nocase(d->name.c_str(),  "lon",    3)==0;
				units_lon = strncmp_nocase(d->units.c_str(), "degree", 6) == 0;
				d->is_lat = ((name_lat || units_lat) && (d->max <  90.01) && (d->min > -90.01));
				d->is_lon = ((name_lon || units_lon) && (d->max < 360.01) && (d->min > -180.01));

				/* There is a funny thing we need to do at this point.  Think about the following case.
				 * We want to look at 3 different files, and they all have a dim named 'lon' in them,
				 * and each is different.  Because this might happen, we can't use the name as an
				 * indication of a unique dimension.  On the other hand, it is very slow to repeatedly
				 * reprocess the same dim over and over, especially if it's the time dim in a series
				 * of virtually concatenated input files.  For that reason, we copy the min and max
				 * values we just found to all identical dims.
				 */
				copyInfoToIdenticalDims( v, d, dim_len );
				}
			}
		}
}

void Dataset::getMinMaxOnestep( NCVar *var, size_t n_other, size_t tstep, float *data,
                                 float *min, float *max, int verbose )
{
	std::vector<size_t> start_v, count_v;
	size_t	n_time;
	size_t	j;
	int	i;
	float	dat, fill_v;

	count_v.resize( var->n_dims );
	start_v.resize( var->n_dims );
	size_t	*start = start_v.data();
	size_t	*count = count_v.data();
	fill_v = var->fill_value;

	n_time = var->size[0];
	if( tstep > (n_time-1) )
		tstep = n_time-1;

	*(count) = 1L;
	*(start) = tstep;
	for( i=1; i<var->n_dims; i++ ) {
		*(start+i) = 0L;
		*(count+i) = var->size[i];
		}

	if( verbose ) {
		printf( "." );
		fflush( stdout );
		}

	fi_get_data( var, start, count, data );

	for( j=0; j<n_other; j++ ) {
		dat = *(data+j);
		if( dat != dat )
			dat = fill_v;
		if( (! close_enough(dat, fill_v)) && (dat != FILL_FLOAT) )
			{
			if( dat > *max )
				*max = dat;
			if( dat < *min )
				*min = dat;
			}
		}
}

void Dataset::checkRanges( NCVar *var )
{
	float	min, max;
	Message	message;
	char	temp_string[ 1024 ];

	if( netcdf_min_max_option_set( var, &min, &max ) ) {
		if( var->global_min < min ) {
			snprintf( temp_string, 1022, "Calculated minimum (%g) is less than\nvalid_range minimum (%g).  Reset\nminimum to valid_range minimum?", var->global_min, min );
			message = in_dialog( temp_string, true );
			if( message == Message::OK )
				var->global_min = min;
			}
		if( var->global_max > max ) {
			snprintf( temp_string, 1022, "Calculated maximum (%g) is greater\nthan valid_range maximum (%g). Reset\nmaximum to valid_range maximum?", var->global_max, max );
			message = in_dialog( temp_string, true );
			if( message == Message::OK )
				var->global_max = max;
			}
		}

	if( netcdf_min_option_set( var, &min ) ) {
		if( var->global_min < min ) {
			snprintf( temp_string, 1022, "Calculated minimum (%g) is less than\nvalid_min minimum (%g).  Reset\nminimum to valid_min value?", var->global_min, min );
			message = in_dialog( temp_string, true );
			if( message == Message::OK )
				var->global_min = min;
			}
		}

	if( netcdf_max_option_set( var, &max ) ) {
		if( var->global_max > max ) {
			snprintf( temp_string, 1022, "Calculated maximum (%g) is greater than\nvalid_max maximum (%g).  Reset\nmaximum to valid_max value?", var->global_max, max );
			message = in_dialog( temp_string, true );
			if( message == Message::OK )
				var->global_max = max;
			}
		}

	var->user_min = var->global_min;
	var->user_max = var->global_max;
	var->have_set_range = true;
}

void Dataset::initMinMax( NCVar *var )
{
	long	n_other, i, step;
	size_t	n_timesteps;
	float	init_min, init_max;
	std::vector<float> data;
	int	verbose;

	init_min =  9.9e30;
	init_max = -9.9e30;
	var->global_min = init_min;
	var->global_max = init_max;

	printf( "calculating min and maxes for %s", var->name.c_str() );

	/* n_other is the number of elements in a single timeslice of the data array */
	n_timesteps = var->size[0];
	n_other     = 1L;
	for( i=1; i<var->n_dims; i++ )
		n_other *= var->size[i];

	data.resize( n_other );

	/* We always get the min and max of the first, middle, and last time
	 * entries if they are distinct.
	 */
	verbose = true;
	step    = 0L;
	getMinMaxOnestep( var, n_other, step, data.data(),
			&(var->global_min), &(var->global_max), verbose );
	if( n_timesteps == 1 ) {
		if( verbose )
			printf( "\n" );
		checkRanges( var );
		return;
		}

	step = n_timesteps-1L;
	getMinMaxOnestep( var, n_other, step, data.data(),
			&(var->global_min), &(var->global_max), verbose );
	if( n_timesteps == 2 ) {
		if( verbose )
			printf( "\n" );
		checkRanges( var );
		return;
		}

	step = (n_timesteps-1L)/2L;
	getMinMaxOnestep( var, n_other, step, data.data(),
			&(var->global_min), &(var->global_max), verbose );
	if( n_timesteps == 3 ) {
		if( verbose )
			printf( "\n" );
		checkRanges( var );
		return;
		}

	switch( options.min_max_method ) {
		case MinMaxMethod::Fast:
			if( verbose )
				printf( "\n" );
			break;

		case MinMaxMethod::Med:
			verbose = true;
			step = (n_timesteps-1L)/4L;
			getMinMaxOnestep( var, n_other, step, data.data(),
				&(var->global_min), &(var->global_max), verbose );
			step = (3L*(n_timesteps-1L))/4L;
			getMinMaxOnestep( var, n_other, step, data.data(),
				&(var->global_min), &(var->global_max), verbose );
			if( verbose )
				printf( "\n" );
			break;

		case MinMaxMethod::Slow:
			verbose = true;
			for( i=2; i<=9; i++ ) {
				printf( "." );
				step = (i*(n_timesteps-1L))/10L;
				getMinMaxOnestep( var, n_other, step, data.data(),
					&(var->global_min), &(var->global_max), verbose );
				}
			if( verbose )
				printf( "\n" );
			break;

		case MinMaxMethod::Exhaust:
			verbose = true;
			for( i=1; i<(long)(n_timesteps-2L); i++ ) {
				step = i;
				getMinMaxOnestep( var, n_other, step, data.data(),
					&(var->global_min), &(var->global_max), verbose );
				}
			if( verbose )
				printf( "\n" );
			break;
		}

	if( (var->global_min == init_min) && (var->global_max == init_max) ) {
		var->global_min = 0.0;
		var->global_max = 0.0;
		}

	checkRanges( var );
}

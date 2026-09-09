/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 2026 Dominik Strebel
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

/*****************************************************************************
 *
 *	The file interface to ncview.
 *
 *	All the routines in this file must be provided for whatever
 *	format data file you want to have.  Ideally, all the information 
 *    	about the data file formats should be encapsulated here.
 *
 *****************************************************************************/

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

static int   file_type;
extern Options options;

/************************************************************************************/
/* Do all file opening and initialization for the passed filename.
 * Return a unique integer ID by which this file will be indicated
 * in the future.
 */
	int
fi_initialize( char *name )
{
	int	id;
	Stringlist *var_list;

	if( file_type == FILE_TYPE_NETCDF ) {
		if( options.debug )
			printf( "Initializing file %s\n", name );
		id = netcdf_fi_initialize( name );
		}
	else
		{
		fprintf( stderr, "?unknown file_type passed to fi_initialize: %d\n",
			file_type );
		exit( -1 );
		}

	if( options.debug )
		printf( "Getting list of variables for file %s\n", name );
	var_list = fi_list_vars( id );
	g_dataset.addVariables( var_list, id, name );

	if( options.debug )
		printf( "Done initializing file %s\n", name );

	return( id );
}

/************************************************************************************/
/* Return a list of the names of all the displayable variables in
 * the file.  Whether or not a variable is "displayable" is determined 
 * by the needs of the data, but in any event any displayable variable
 * must have at least 1 scannable dimension and be accessable by these 
 * routines. If the user wouldn't ever be interested in contouring some
 * variable, such as a dimension variable, it shouldn't appear on this list.
 */
	Stringlist *
fi_list_vars( int fileid )
{
	if( file_type != FILE_TYPE_NETCDF )
		{
		fprintf( stderr, "?unknown file_type passed to fi_list_vars: %d\n",
			file_type );
		exit( -1 );
		}
	return( netcdf_fi_list_vars( fileid ));
}

/************************************************************************************
 * Return the "title" of the file, if applicable.  Otherwise, return NULL.
 */
	std::string
fi_title( int fileid )
{
	if( file_type != FILE_TYPE_NETCDF )
		{
		fprintf( stderr, "?unknown file_type passed to fi_title: %d\n",
			file_type );
		exit( -1 );
		}
	return( netcdf_title( fileid ));
}

/************************************************************************************
 * Return the 'long name' of a variable, if appropriate.  Otherwise, return empty.
 */
	std::string
fi_long_var_name( int fileid, std::string_view var_name )
{
	if( file_type != FILE_TYPE_NETCDF )
		{
		fprintf( stderr, "?unknown file_type passed to fi_title: %d\n",
			file_type );
		exit( -1 );
		}
	return( netcdf_long_var_name( fileid, var_name ));
}

/************************************************************************************
 * Return the 'units' of a variable, if appropriate.  Otherwise, return empty.
 */
	std::string
fi_var_units( int fileid, std::string_view var_name )
{
	if( file_type != FILE_TYPE_NETCDF )
		{
		fprintf( stderr, "?unknown file_type passed to fi_var_units: %d\n",
			file_type );
		exit( -1 );
		}
	return( netcdf_var_units( fileid, var_name ));
}

/************************************************************************************
 * Return the 'calendar' attribution of a dimension, if appropriate.  Otherwise, return empty.
 */
	std::string
fi_dim_calendar( int fileid, std::string_view dim_name )
{
	/* Command line specified calendar OVERRIDES info in the file */
	if( ! options.calendar.empty() )
		return options.calendar;

	if( file_type != FILE_TYPE_NETCDF )
		{
		fprintf( stderr, "?unknown file_type passed to fi_dim_calendar: %d\n",
			file_type );
		exit( -1 );
		}
	return( netcdf_dim_calendar( fileid, dim_name ));
}

/************************************************************************************
 * Return the 'units' of a dimension, if appropriate.  Otherwise, return empty.
 */
	std::string
fi_dim_units( int fileid, std::string_view dim_name )
{
	if( file_type != FILE_TYPE_NETCDF )
		{
		fprintf( stderr, "?unknown file_type passed to fi_dim_units: %d\n",
			file_type );
		exit( -1 );
		}
	return( netcdf_dim_units( fileid, dim_name ));
}

/************************************************************************************/
/* Given a file id and the name of a variable, return the number of 
 * dimensions which that variable has.  This reads it directly from
 * the file, not using information in the 'variables' structure.
 */
	int
fi_n_dims( int id, char *var_name )
{
	if( file_type != FILE_TYPE_NETCDF )
		{
		fprintf( stderr, "?unknown file_type passed to fi_n_dims: %d\n",
			file_type );
		exit( -1 );
		}
	return( netcdf_fi_n_dims( id, var_name ));
}

/***********************************************************************************
 * Given a variable name, return a Stringlist of "scannable" dimensions for it.  The
 * definition of "scannable" dimension is rather loose; I'm using the assumption
 * that a scannable dimension must have a minimum number of points along it.
 * This is set in the routine netcdf_scannable_dims.
 */
	Stringlist *
fi_scannable_dims( int fileid, char *var_name )
{
	if( file_type != FILE_TYPE_NETCDF )
		{
		fprintf( stderr, "?unknown file_type passed to fi_scannable_dims: %d\n",
			file_type );
		exit( -1 );
		}
	return( netcdf_scannable_dims( fileid, var_name ));
}

/************************************************************************************
 * Given the file and the name of a variable in it, return an array
 * of the dimension sizes for that variable.  This reads the information
 * directly from the file, not from the 'variables' structure.
 */
	size_t *
fi_var_size( int fileid, char *var_name )
{
	if( file_type != FILE_TYPE_NETCDF )
		{
		fprintf( stderr, "?unknown file_type passed to fi_var_size: %d\n",
			file_type );
		exit( -1 );
		}
	return( netcdf_fi_var_size( fileid, var_name ));
}

/************************************************************************************
 * Given a dimension's id and the name of the variable who owns it,
 * return the name of the dimension.  'Id' here means the index into
 * the size_array of the owning variable.
 */
	std::string
fi_dim_id_to_name( int fileid, std::string_view var_name, int dim_id )
{
	if( file_type != FILE_TYPE_NETCDF )
		{
		fprintf( stderr, "?unknown file_type passed to fi_dim_id_to_name: %d\n",
			file_type );
		exit( -1 );
		}
	return( netcdf_dim_id_to_name( fileid, var_name, dim_id ));
}

/************************************************************************************
 * Given a dimension's name and the name of the variable who owns it,
 * return the index where that dimension appears in the size array
 * returned by 'fi_var_size'.  Return -1 if the dimension does not 
 * appear in the variable.
 */
	int
fi_dim_name_to_id( int fileid, char *var_name, char *dim_name )
{
	if( file_type != FILE_TYPE_NETCDF ) {
		fprintf( stderr, "?unknown file_type passed to fi_var_size: %d\n",
			file_type );
		exit( -1 );
		}
	return( netcdf_dim_name_to_id( fileid, var_name, dim_name ));
}

/************************************************************************************
 * Close the relevant file 
 */
	void
fi_close( int fileid )
{
	if( file_type == FILE_TYPE_NETCDF )
		netcdf_fi_close( fileid );
	else
		{
		fprintf( stderr, "?unknown file_type passed to fi_close: %d\n",
			file_type );
		exit( -1 );
		}
}

/*************************************************************************************
 * Does this dimension have a longname?  If so, return it.  Otherwise, NULL.
 */
	std::string
fi_dim_longname( int fileid, std::string_view dim_name )
{
	if( file_type != FILE_TYPE_NETCDF )
		{
		fprintf( stderr, "?unknown file_type passed to fi_has_dim_values: %d\n",
			file_type );
		exit( -1 );
		}
	return( netcdf_dim_longname( fileid, dim_name ) );
}

/*************************************************************************************
 * File utility routines; things below this line shouldn't have to be changed 
 * for different data file formats.
 */

	void
determine_file_type( Stringlist *input_files )
{
	int		ierr;
	struct stat 	buf;

	if( input_files == NULL ) {
		fprintf( stderr, "ncview: takes at least one file name as argument\n" );
		useage();
		exit( -1 );
		}

	const char *first_file = (*input_files)[0].string.c_str();

	if( netcdf_fi_confirm( (char *)first_file ) )
		file_type = FILE_TYPE_NETCDF;
	else
		{
		ierr = stat( first_file, &buf );
		if( ierr == 0 ) {
			fprintf( stderr, "ncview: can't recognize format of input file %s\n",
				first_file );
			exit( -1 );
			}
		else
			{
			fprintf( stderr, "ncview: can't open file %s",
				first_file );
			perror(" ");
			exit( -1 );
			}
		}
}

/************************************************************************************
 * Fill out the data structure which holds information unique to each data file
 * format.
 */
	void
fi_fill_aux_data( int id, char *var_name, FDBlist *fdb )
{
	if( file_type == FILE_TYPE_NETCDF )
		netcdf_fill_aux_data( id, var_name, fdb );
	else
		{
		fprintf( stderr, "?unknown file_type passed to fi_has_dim_values: %d\n",
			file_type );
		exit( -1 );
		}
}

	int
fi_recdim_id( int fileid )
{
	return( netcdf_fi_recdim_id( fileid ));
}

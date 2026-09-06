/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 1993 through 2024 by David W. Pierce
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
	ncview.defines.h

	#defines, and structure definitions
*/

#pragma once

#ifdef HAVE_UDUNITS2
#include <udunits2.h>
#endif

#include <memory>
#include <string>
#include <vector>

/* X11's <X11/X.h> visual-class constant, value 3, used by util.cc's
 * data_to_pixels() to decide whether to run pixel values through the
 * indexed-colormap pixel_transform table. Defined here (rather than pulling
 * in X11 headers) so core stays UI-free; ncview_ui sets options.display_type
 * to this value if/when it still needs indexed-colormap emulation. */
#define PseudoColor 3

#define PROGRAM_ID		"Ncview 2.1.11 David W. Pierce 7 November 2024"
#define PROGRAM_VERSION_STRING	"2.1.11"
constexpr double APP_RES_VERSION = 1.93;

/******************** Buttons in the user interface **********************/
enum class Button {
	Rewind		= 1,
	Backwards	= 2,
	Pause		= 3,
	Forward		= 4,
	Fastforward	= 5,
	ColormapSelect	= 6,	/* this is also a label */
	InvertPhysical	= 7,
	InvertColormap	= 8,
	Minimum		= 9,
	Maximum		= 10,
	Quit		= 11,
	Blowup		= 12,	/* this is also a label */
	Restart		= 13,
	Transform	= 14,	/* this is also a label */
	Dimset		= 15,
	Range		= 16,
	BlowupType	= 17,	/* this is also a label */
	Skip		= 18,	/* this is also a label */
	Edit		= 19,
	Info		= 20,
	Print		= 21,
	Options		= 22,
};

/***************************************************************************
 * These are the overlays we know about
 */
#define OVERLAY_NONE			0
#define OVERLAY_P8DEG			1
#define OVERLAY_P08DEG			2
#define OVERLAY_USA			3
#define OVERLAY_CUSTOM			4

constexpr int OVERLAY_N_OVERLAYS = 5;


/***************************************************************************
 * General purpose writable labels in the user interface. Each numbered
 * slot (L1..L6) also has a semantic name for the one thing it's actually
 * used to display (Title, ScanvarName, ...) -- both names are the same
 * enumerator, exactly as the old LABEL_TITLE/LABEL_1-style #define aliasing
 * was.
 */
enum class Label {
	L1		= 1,
	L2		= 2,
	L3		= 3,
	L4		= 4,
	/* Specific purpose writable labels in the user interface. */
	ColormapName	= 5,	/* this is also a button */
	Blowup		= 6,	/* this is also a button */
	Transform	= 7,
	CcInfo1		= 8,
	CcInfo2		= 9,
	BlowupType	= 10,	/* this is also a button */
	L5		= 11,
	Skip		= 12,	/* this is also a button */
	L6		= 13,

	Title		= L1,
	ScanvarName	= L2,
	ScanPlace	= L3,
	DataExtrema	= L4,
	DataValue	= L5,
	ScalarDims	= L6,	/* information from the scalar dims */
};

/*****************************************************************************/
/* Transforming the data before turning it into pixels is supported */
constexpr int N_TRANSFORMS = 4;
enum class Transform { None = 1, Low = 2, Hi = 3, Center = 4 };

/*****************************************************************************
 * Maximum number of X-Y plot windows which can pop up, and the max
 * number of lines on one plot.
 */
constexpr int MAX_PLOT_XY = 10;
constexpr int MAX_LINES_PER_PLOT = 5;

/*****************************************************************************
 * This is for the popup windows which show all of a variable's attributes.
 */
constexpr int MAX_DISPLAY_POPUPS = 10;

/*****************************************************************************/
/* Types of file data formats supported */
constexpr int FILE_TYPE_NETCDF = 1;

/*****************************************************************************/
/* Maximum name length of a variable */
constexpr int MAX_VAR_NAME_LEN = 4095;

/*****************************************************************************/
/* Maximum name length of a file */
constexpr int MAX_FILE_NAME_LEN = 4095;

/*****************************************************************************/
/* Maximum name length of a recdim units */
constexpr int MAX_RECDIM_UNITS_LEN = 4095;

/*****************************************************************************/
/* Possible interpretations for the change_view routine; either change
 * the specified number of FRAMES or the specified PERCENT.
 */
constexpr int FRAMES = 1;
constexpr int PERCENT = 2;

/*****************************************************************************/
/* Truncate displayed strings which are longer than this */
constexpr int MAX_DISPLAYED_STRING_LENGTH = 250;

/*****************************************************************************/
/* What dimension button sets we have */
enum class Dimension { X = 1, Y = 2, Scan = 3, None = 4 };

/*****************************************************************************/
/* Button-press modification indicators */
enum class Modifier { M1 = 1, M2 = 2, M3 = 3, M4 = 4 };

/*****************************************************************************/
/* Messages which a dialog popup can return */
enum class Message { OK = 1, Cancel = 2 };

/*****************************************************************************/
/* This is used in x_interface.c, even though it has nothing to do with
 * the X interface, because the X mechanism has a way of
 * reading in resource files, and there is no point in reading in TWO
 * different configuration files.  Sigh.  For use of this, see routine
 * 'check_app_res' in file x_interface.c
 */
constexpr int DEFAULT_DELTA_STEP = 10;

/*****************************************************************************/
/* Ways in which the file's min and max can be calculated */
enum class MinMaxMethod { Fast = 1, Med = 2, Slow = 3, Exhaust = 4 };

/*****************************************************************************/
/* Data which has the fill_value is IGNORED.  It is assumed to represent
 * out of domain or out of range data.  Netcdf has its own values for this
 * which replace this value, so in Netcdf implementations, this particular
 * value is not the one which is actually used.
 */
constexpr float DEFAULT_FILL_VALUE = 1.0e35f;

/*******************************************************************
 * Ways to expand a small pixmap into a large one.
 */
enum class BlowupType { Replicate = 1, Bilinear = 2 };

/*******************************************************************
 * Ways to contract a large pixmap into a small one.
 */
enum class ShrinkMethod { Mean = 0, Mode = 1 };

/*********************************************************************
 * Possible states which the data inside the current buffer can be in
 */
enum class ViewDataStatus { Valid = 1, Invalid = 2, Edited = 3 };

/*******************************************************************
 * Where postscript output can go.
 */
enum class Device { Printer = 1, File = 2 };

/*******************************************************************
 * Ways of handling the variable-select area.  We can either list
 * all the variables, or make a pull-down menu for selecting them.
 */
enum class VarselStyle { List = 1, Menu = 2 };

/*******************************************************************
 * Recognized standards by which the time axis may be described
 */
enum class TimeStandard {
	Udunits = 1,	/* Ex: units="days since 1900-01-01" */
	Epic0   = 2,	/* Ex: units="True Julian Day" w/att epic_code=624 */
	Months  = 3,	/* Ex: units="months", Jan 1 AD = month 1 */
};

/*******************************************************************
 * Kinds of time-like granularity.
 */
enum class TimeGranularity { Sec = 1, Min = 2, Hour = 3, Day = 4, Month = 5, Year = 6 };

/*******************************************************************
 * Maximum number of scalar "coord" attributes we can have for
 * variable.
 */
constexpr int MAX_SCALAR_COORDS = 20;

/*******************************************************************
 *
 * 	The main concept here is the 'variable'.  Variables are
 *	things which might possibly be displayed by ncview.  Variables
 *	live in one or more files, and within each of those files 
 *	have a size, and minimum and maximum values.  Different 
 *	variables can be in different files, but if you have the 
 *	same variable in different files it must have the EXACT SAME
 *	layout in all files, with the exception of the first index
 *	(which is the time index in netCDF files).  So, you can have
 *	20 time entries in the first file, then 7 in the second, and
 *	14 in the third; but you can't have the resolution of the 
 *	variable be different in the different files.
 *
 ********************************************************************/

/*****************************************************************************/
typedef unsigned char ncv_pixel;/* If you change this, make sure to change
				 * routine 'data_to_pixels' in util.c!  It
				 * assumes a size of one byte.  Some of the
				 * X routines do also.
				 */

/*****************************************************************************
 * A specific set of data for netCDF-type files.  These won't necessarily
 * be applicable to different types of data file formats.
 */
typedef struct {
	int	valid_range_set,
		valid_min_set,
		valid_max_set,
		scale_factor_set,
		add_offset_set;

	float	valid_range[2],
		valid_min,
		valid_max,
		scale_factor,
		add_offset;

} NetCDFOptions;

struct NCVar;	/* forward declaration -- NCDim_map_info::var_i_map is a non-owning back-pointer to
		 * the NCVar it maps; NCVar itself is defined below since it owns FDBlist/NCDim/
		 * NCDim_map_info via std::vector<std::unique_ptr<...>>. */

/*****************************************************************************/
/* This describes the file which the relevant variable lives in */
struct FDBlist {
	int	id = 0;		/* internally used ID number */
	int	index = 0;	/* starts at 0, increments by 1 for each file associated
				 * with this variable */
	std::string	filename;
	std::unique_ptr<NetCDFOptions>	aux_data;	/* For specific datafile implementations */
	std::vector<size_t>	var_size;	/* Multi-dimensional size of variables which live in this file */
	float	data_min = 0, data_max = 0; /* for a specific variable in the file */

	/* Following is an ugly hack for an ugly problem.  Basically, different files can have
	 * different units for the unlimited dimension, and some people actually do this.  So
	 * we must store the recdim units for each file.  In a way this is a property more of
	 * the dimensions, so maybe should be in the NCDim structure somehow, but the units live
	 * in each file and have a 1-1 association with each file, so I'm putting them here.
	 */
	std::string	recdim_units;
#ifdef HAVE_UDUNITS2
	ut_unit	*ut_unit_ptr = nullptr;	/* only non-null if ut_parse worked on these units; owned by udunits2, not us */
#endif
};

/*****************************************************************************/
/* The dimension structure.  This is more for convienence and efficiency
 * than because dimensions are so fundamental; actually, it's the variables
 * which are more important.
 */
typedef struct {
	std::string	name, long_name, units;
	int	units_change = 0;	/* if 1, then a virtully concatenated timelike dimension has different units in different input files */
	float	min = 0, max = 0;
	std::vector<float> values;
	int	have_calc_minmax = 0;  /* 0 initially, 1 after min & max have been calculated */
	size_t	size = 0;
	int	timelike = 0;	/* 0 if NOT timelike, 1 if is.  If is, MUST */
				/* have an identified time standard (below). */
	TimeStandard	time_std;	/* TimeStandard::Udunits, Epic0, Months */
	std::string	calendar;	/* ONLY applicable if time_std==TimeStandard::Udunits; can be any CF-1.0 value. Defaults to "standard" */
	TimeGranularity	tgran; 		/* time granularity; i.e., frequency of entries (daily, hourly, etc) */
	int	global_id = 0;	/* Used internally, goes from 1..total number of dims we know about */
	int	is_lat = 0, is_lon = 0; /* Just a guess if these are lat/lon. Used to put on coastlines automatically */
} NCDim;

/*****************************************************************************/
/* A dimension can be "mapped", by which it means that, for example, the lat
 * or lon coordinates are two dimensional, and a variable is supplied that
 * gives the lat and/or lon values as a function of X and Y.
 */
typedef struct {

	NCVar	*var_i_map = nullptr;	/* the "var that I map"; non-owning back-pointer */
	std::string	coord_att;		/* Contents of the "coordinates" attribute */
	std::string	coord_var_name;	/* Name of the VAR in the file that holds mapping info */
	int	coord_var_ndims = 0;	/* # of dims in the mapping var */
	std::string	coord_var_units;	/* if the coord var has a units att, this records it */
	std::vector<size_t> coord_var_size;	/* size of the mapping var (MULTIDIMENSIONAL) */
	std::vector<int> matching_var_dims;	/* This has n_dims equal to the DATA VARIABLE, NOT the coord var! */
	std::vector<float> data_cache;		/* Cached info from the mapping var */
	std::vector<size_t> index_place_factor;	/* Array of size var_i_map->n_dims, is 0 or factor to mult loc by */
	int	scalar_all_same = 0;	/* ==1 iff is a scalar coord var AND all vals are identical; ==0 otherwise */

} NCDim_map_info;

/*****************************************************************************/
/* Here it is: the variable structure.  Aspects of the variable which are
 * different from file to file are kept in the pointed-to file descriptor
 * blocks (FDBs).
 */
struct NCVar {
	std::string	name;
	float	fill_value = 0;			/* Any data with this special
						 * value will be IGNORED. It
						 * is assumed to indicate
						 * out-of-range or out-of-domain
						 * data.
						 */
	bool	have_set_range = false;	/* have we set the valid range for this var yet? */
	int	n_dims = 0;				/* how many dimensions this var has */
	std::vector<std::unique_ptr<FDBlist>> files;	/* What files this variable lives in */
	int	is_virtual = 0;			/* Boolean -- true if this var lives
						 * in more than one input file, false
						 * otherwise.
						 */
	std::vector<FDBlist*> timestep_2_fdb;	/* Files can only be virtually concatenated
	  					 * along the first (timelike) dimension.
						 * This gives pointers (non-owning; owned by
						 * 'files' above) to the FDBlist element that
						 * each individual timestep of the var lives
						 * in. So it is dimension size[0]. Note that
						 * many entries can point to the same target.
						 * For example, if a sequency of data files
						 * with a year's worth of monthly data is
						 * given, then the first 12 entries of this
						 * will all point to the first file's FDBlist.
						 * Because this can only be filled out
						 * AFTER we have processed all the files,
						 * it is done in a slighly strange place...
						 * in routine cache_scalar_coord_info.
						 */
	float	global_min = 0, global_max = 0,		/* These are diffferent from the */
	        user_min = 0, user_max = 0;	 	/* min & max in the FDBs because these
					 	* are global, rather than local to
					 	* a file.
					 	*/
	int	user_set_blowup = 0;		/* Initializes to -99999, then saves user-specified
	  					 * value of 'blowup' for this var so it can be
						 * used again if we leave this var & then come back
						 */
	int	auto_set_no_range = 0;		/* '1' if we autoset a range of -1,1 based
						 * on not having a valid range for this var
						 */
	std::vector<size_t> size;			/* The accumulated size of
						 * this variable, from all
						 * the files which hold it.
						 */
	int  	effective_dimensionality = 0;	/* # of entries in 'size' array > 1 */
	std::vector<std::unique_ptr<NCDim>> dim;	/* An array of 'n_dim' entries.  This
	 					 * is only filled out for
	 					 * scannable dimensions!! If
	 					 * the dim is not scannable,
	 					 * a null entry is inserted instead.
	 					 */
	std::vector<std::unique_ptr<NCDim_map_info>> dim_map_info;	/* array of ndims entries
						   that hold information describing
						   the 2-D mapping for this dim.
						   This vector itself should never be
						   empty of entries (it always has ndims
						   slots), but the entries CAN BE NULL,
						   in which event that dim has
						   no mapping.  Note that the
						   dim mapping is a function of
						   the VARIABLE rather than the
						   dim, which is an oddity of the
						   way CF conventions handle mapping.
						 */
	std::vector<std::unique_ptr<NCDim_map_info>> scalar_dim_map_info; 	/* A variable can also
						   specify SCALAR coordinate "vars",
						   for example, "height" for a 2d
						   field in (lon,lat). This holds
						   the scalar info. The reason this
						   is not part of athe dim_map_info
						   array is that array has exactly
						   ndims entries, describing how
						   each dim in the var is mapped.
						   This array can have many more
						   scalar "dims", locating the field
						   in space or time. They are really
						   different concepts but the CF
						   convention puts them both in
						   the "coordinates" attribute.
						   */
};

/*****************************************************************************/
/* Our current view--the view is the 2D field which is being color-contoured.
 */
typedef struct {
	NCVar	*variable;
	std::vector<size_t>	var_place;	/* Where we currently are in that var's space, in that file */
	std::vector<float>	data;		/* The actual 2-D data to colorcontour */
	ViewDataStatus	data_status;	/* Either valid, invalid, or edited (changed) */
	std::vector<ncv_pixel>	pixels;	/* Scaled, replicated, byte array version of data */
	int	x_axis_id, 	/* which axes the 2-D data lies on.  'scan' */
		y_axis_id,	/* is the one accessed by the pushbuttons */
		scan_axis_id;
	int	skip;		/* Number of time entries to stride each time */
	int	plot_XY_axis,	/* Which axis to plot along in XY plots */
		plot_XY_nlines;	/* # of XY lines for this variable on current plot */
	size_t	plot_XY_position[MAX_LINES_PER_PLOT][10];
} View;

/*****************************************************************************
 * Place to store the frames in, if we want in-core displaying.
 */
typedef struct {
	int	valid;		/* Is ANYTHING in the frame store valid? */
	size_t	nt;		/* # of frames in the store.  Can be > than nt cuz we allocate some extra to handle file growth */
	size_t	nx, ny;		/* # of X and Y entries per frame */
	std::vector<ncv_pixel> frame;	/* Actual store of the frames */
	std::vector<int> frame_valid;	/* Is this particular frame valid? */
} FrameStore;

/*****************************************************************************/
/* program options */

/* Options for the overlay feature */
typedef struct {
	int	doit;
	std::vector<int>	overlay;
} OverlayOptions;

typedef struct {
	int	invert_physical,
		invert_colors,
		t_conv,
		debug,
		show_sel,
		no_autoflip,
		no_char_dims,
		private_colormap,
		want_extra_info,
		n_colors,
		n_extra_colors,	/* Supposedly for black, white, etc., but not used much nowadays */
		small,
		dump_frames,
		no_1d_vars,
		delta_step,	/* if > 0, percent of total frames to step when pressing the
				 * 'forward' or 'backward' button and holding down the Ctrl
				 * key; if < 0, absolute number of frames to step.
				 */
		listsel_max,	/* if # of vars is more than this, auto switch from VARSEL_LIST to VARSEL_MENU */
		color_by_ndims,	/* if 1, then button is color coded by # of effective dims */
		beep_on_restart,
		stop_on_restart,
		auto_overlay,	/* if 1, then tries to figure out if coastlines should automatically be added */
		blowup,
		maxsize_pct,	/* -1 if a width/height pair specified instead */
		maxsize_width,	/* in pixels */
		maxsize_height,	/* in pixels */
		blowup_default_size,
		display_type;	/* This uses std 'X' defines; PseudoColor, DirectColor, etc */

	Transform	transform;
	MinMaxMethod	min_max_method;
	VarselStyle	varsel_style;	/* can be VarselStyle::List or VarselStyle::Menu */
	ShrinkMethod	shrink_method;

	std::string	ncview_base_dir,	/* apparently dead: never read as options.ncview_base_dir anywhere -- see modernization.md Phase 6 */
			window_title,		/* apparently dead: written but never read anywhere -- see modernization.md Phase 6 */
			calendar;	/* This OVERRIDES any 'calendar' attribute in the data file; empty means "not set" */

	BlowupType	blowup_type;	/* can be BlowupType::Replicate or BlowupType::Bilinear */

	int	autoscale;	/* If TRUE, then tries to automatically scale colors for EACH frame.  Much slower!! */

	int	save_frames;	/* If true, try to save frames in core for faster display */
	float	frame_delay;	/* Normalied to be between 0.0 and 1.0 */

	int	enable_group_sel;	/* TRUE if we have some vars in groups, so interface must incl. grp selection */

	int	missval_r, missval_g, missval_b;	/* 0-255 values of R, G, B for missing data */

	float	scale, offset;	/* These do NOT refer to the scale & offset in the netcdf file. They are for changing units of data */
				/* SCALE IS APPLIED FIRST. So to conv C to F, use -scale 1.8 -offset 32 */

	std::unique_ptr<OverlayOptions> overlay;
} Options;

/***********************************************************************************************************/
/* Postscript printer output options */
typedef struct {
	float	page_width, page_height,		/* In inches */
		page_x_margin, page_upper_y_margin,	/* In inches */
		page_lower_y_margin, ppi;		/* Points per inch */
	int	font_size,
		leading,
		header_font_size;		/* In points */
	std::string	font_name,			/* Postscript name */
			out_file_name;
	Device	output_device;
	int	include_outline,
		include_id,
		include_title,
		include_axis_labels,
		include_extra_info,
		test_only;
} PrintOptions;


/*
 * core/include/ncview/dataset.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * NetCDFFile: move-only RAII ownership of one nc_open'd file, closing it
 * exactly once on destruction via the existing fi_close()/netcdf_fi_close()
 * machinery (file.cc/file_netcdf.cc). Before this, no file ncview opened
 * was ever closed -- fi_close() had zero callers anywhere in core/ui.
 *
 * Dataset: owns every NCVar (replacing the global `variables`) and every
 * NetCDFFile a session has opened. A single physical file is opened once
 * (addVariable() below is called once per (file, variable name) pair, but
 * with the SAME netCDF fileid for every variable that file contains), so
 * trackFile() deduplicates by fileid: the first FDBlist for a given
 * fileid causes Dataset to take ownership of it; every subsequent FDBlist
 * for that same fileid gets a pointer to the already-tracked NetCDFFile
 * instead of a second, competing owner.
 *
 * OOP_redesign plan, Step 5 introduced the ownership and bridged the
 * legacy global `variables` onto Dataset's storage (see ncview.cc); a
 * later pass moved the functions that actually build/mutate that list
 * (formerly free functions in util.cc: add_var_to_list, add_vars_to_list,
 * get_var, cache_scalar_coord_info, calc_dim_minmaxes, init_min_max, plus
 * their file-private helpers check_ranges/get_min_max_onestep/
 * copy_info_to_identical_dims/equivalent_FDBs/new_fdblist) onto Dataset
 * as real methods, with every call site updated directly -- no
 * free-function forwarding shim kept for compatibility. Functions that
 * only fill in fields of an already-allocated NCVar* from netCDF metadata,
 * without touching the variable list itself (fill_dim_structs,
 * handle_dim_mapping, is_scannable, ...), stay free functions -- moved to
 * var_metadata.cc when util.cc was dissolved (Phase 4b of the "refine the
 * architecture" plan), still called from Dataset's methods the same way
 * anything else calls them.
 */
#pragma once

#include <memory>
#include <vector>

#include "ncview/defines.h"
#include "ncview/stringlist.h"

class NetCDFFile {
public:
	explicit NetCDFFile( int fileid ) : fileid_( fileid ) {}
	~NetCDFFile();

	NetCDFFile( const NetCDFFile & ) = delete;
	NetCDFFile &operator=( const NetCDFFile & ) = delete;

	NetCDFFile( NetCDFFile &&other ) noexcept : fileid_( other.fileid_ ) {
		other.fileid_ = -1;
	}
	NetCDFFile &operator=( NetCDFFile &&other ) noexcept {
		if( this != &other ) {
			close();
			fileid_ = other.fileid_;
			other.fileid_ = -1;
			}
		return *this;
	}

	int id() const { return fileid_; }

private:
	void close();
	int fileid_;
};

class Dataset {
public:
	Dataset() = default;

	std::vector<std::unique_ptr<NCVar>> &variablesMutable() { return variables_; }
	const std::vector<std::unique_ptr<NCVar>> &variables() const { return variables_; }

	/* Returns the NetCDFFile that owns 'fileid', taking ownership of it
	 * (Dataset will close it on destruction) the first time this fileid
	 * is seen; returns the same NetCDFFile* on every later call with the
	 * same fileid. */
	NetCDFFile *trackFile( int fileid );

	/* Formerly util.cc's get_var(): a plain linear scan by name. */
	NCVar *findVariable( const char *var_name );

	/* Formerly util.cc's add_var_to_list()/add_vars_to_list(): fill out
	 * the FDBlist/NCVar structures for the given variable(s) and add them
	 * to (or extend an existing entry in) variables_. */
	void addVariable( const char *var_name, int file_id, const char *filename, int nfiles );
	void addVariables( Stringlist *var_list, int id, const char *filename, int nfiles );

	/* Formerly util.cc's cache_scalar_coord_info(): builds timestep_2_fdb
	 * and the scalar-coordinate data cache for every variable currently
	 * on the list. Must run after all files have been added. */
	void cacheScalarCoordInfo();

	/* Formerly util.cc's calc_dim_minmaxes(): computes min/max (and a
	 * lat/lon guess) for every not-yet-processed NCDim referenced by any
	 * variable on the list. */
	void calcDimMinmaxes();

	/* Formerly util.cc's init_min_max(): samples a variable's data to
	 * establish its global min/max, then reconciles that against any
	 * valid_range/valid_min/valid_max attribute (possibly prompting via
	 * in_dialog()). */
	void initMinMax( NCVar *var );

	/* Formerly util.cc's get_min_max_onestep(): reads one timestep's data
	 * and folds its extrema into min/max. Public: initMinMax() uses it
	 * internally to sample several timesteps, but view.cc's
	 * view_check_new_data() also calls it directly (to check a single
	 * newly-arrived timestep against an as-yet-unset range), so it's a
	 * genuinely shared operation, not a private implementation detail of
	 * initMinMax() alone. */
	void getMinMaxOnestep( NCVar *var, size_t n_other, size_t tstep, float *data,
	                        float *min, float *max, int verbose );

private:
	/* Declared before variables_ so it's destroyed AFTER variables_ --
	 * member destruction runs in reverse declaration order, and nothing
	 * should still be holding an FDBlist::file pointer once these
	 * NetCDFFiles start closing. */
	std::vector<std::unique_ptr<NetCDFFile>> files_;
	std::vector<std::unique_ptr<NCVar>> variables_;

	/* initMinMax()'s only caller of this -- formerly util.cc's
	 * check_ranges(), with no other external callers, so it moved along
	 * with initMinMax() as an implementation detail rather than becoming
	 * a public Dataset method. */
	void checkRanges( NCVar *var );

	/* calcDimMinmaxes()'s only caller of this -- formerly util.cc's
	 * copy_info_to_identical_dims(), which directly scanned the global
	 * variable list the same way calcDimMinmaxes() itself does. */
	void copyInfoToIdenticalDims( NCVar *vsrc, NCDim *dsrc, size_t dim_len );
};

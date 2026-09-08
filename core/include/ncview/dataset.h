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
 * (add_var_to_list() in util.cc is called once per (file, variable name)
 * pair, but with the SAME netCDF fileid for every variable that file
 * contains), so trackFile() deduplicates by fileid: the first FDBlist for
 * a given fileid causes Dataset to take ownership of it; every subsequent
 * FDBlist for that same fileid gets a pointer to the already-tracked
 * NetCDFFile instead of a second, competing owner.
 *
 * OOP_redesign plan, Step 5. This step only introduces the ownership and
 * bridges the legacy global `variables` onto Dataset's storage (see
 * ncview.cc) -- it deliberately does not yet change the ~80 call sites
 * that read NCVar/FDBlist through that global, nor introduce the fuller
 * Dataset API (findVariable/readSlice) the original plan sketched; those
 * are follow-on work once ViewerSession exists to hold Dataset directly.
 */
#pragma once

#include <memory>
#include <vector>

#include "ncview/defines.h"

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

private:
	/* Declared before variables_ so it's destroyed AFTER variables_ --
	 * member destruction runs in reverse declaration order, and nothing
	 * should still be holding an FDBlist::file pointer once these
	 * NetCDFFiles start closing. */
	std::vector<std::unique_ptr<NetCDFFile>> files_;
	std::vector<std::unique_ptr<NCVar>> variables_;
};

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

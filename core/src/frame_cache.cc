/*
 * core/src/frame_cache.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * See ncview/frame_cache.h. OOP_redesign plan, Step 3.
 */
#include "ncview/frame_cache.h"

void FrameCache::reset( size_t nt, size_t nx, size_t ny )
{
	frame_.clear();
	frame_.shrink_to_fit();
	frame_valid_.clear();
	frame_valid_.shrink_to_fit();
	valid_ = false;

	nt_ = nt;
	nx_ = nx;
	ny_ = ny;
	size_t storage_size = nx_ * ny_ * nt_;

	try {
		frame_.resize( storage_size );
		frame_valid_.resize( nt_, false );
		valid_ = true;
		}
	catch( const std::bad_alloc & ) {
		frame_.clear();
		frame_.shrink_to_fit();
		frame_valid_.clear();
		frame_valid_.shrink_to_fit();
		valid_ = false;
		throw;
		}
}

void FrameCache::growTo( size_t new_nt )
{
	size_t old_nt = nt_;
	nt_ = new_nt;
	frame_.resize( nx_ * ny_ * nt_ );
	frame_valid_.resize( nt_ );

	/* Initialize to NOT a valid frame for the new frames */
	for( size_t i=old_nt; i<nt_; i++ )
		frame_valid_[i] = false;
}

void FrameCache::invalidateAll()
{
	for( size_t i=0; i<nt_; i++ )
		frame_valid_[i] = false;
}

const ncv_pixel* FrameCache::lookup( size_t frameno ) const
{
	if( ! valid_ )
		return nullptr;
	if( (frameno >= frame_valid_.size()) || ! frame_valid_[frameno] )
		return nullptr;
	return frame_.data() + frameno * (nx_ * ny_);
}

void FrameCache::store( size_t frameno, const ncv_pixel *pixels, size_t count )
{
	size_t offset = frameno * count;
	for( size_t i=0; i<count; i++ )
		frame_[offset + i] = pixels[i];
	if( frameno < frame_valid_.size() )
		frame_valid_[frameno] = true;
}

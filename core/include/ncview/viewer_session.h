/*
 * core/include/ncview/viewer_session.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * ViewerSession -- OOP_redesign plan, Step 8. Owns the pieces of runtime
 * state that used to be independent globals: the Dataset (Step 5), the
 * active ViewState (Step 6), and the FrameCache (Step 3). A single global
 * ViewerSession instance is constructed in ncview.cc; the legacy globals
 * `g_dataset`, `view`, and `framestore` become references bound to this
 * instance's members (same migration-bridge technique Step 5 used for
 * `variables`), so every existing call site across core/, ui/, and tests/
 * keeps compiling and behaving identically.
 *
 * Options field classification: this step's plan section also calls for
 * splitting the ~50-field global Options struct into render/playback/
 * session/startup groups. That classification is recorded as comments on
 * the Options struct itself (see defines.h) rather than duplicated here as
 * a second, unused struct -- Options's storage stays a single global for
 * now, since migrating its ~300+ read/write call sites is a separate,
 * much larger undertaking than this step's scope (see viewer_session.cc's
 * header comment for the full rationale). The one real, verified migration
 * this step makes on the Options side is pixelMapSettings() below: the
 * render-shaped fields' one consumer (FrameRenderer, via util.cc's
 * data_to_pixels()) now goes through ViewerSession instead of being
 * built inline.
 */
#pragma once

#include <memory>

#include "ncview/dataset.h"
#include "ncview/defines.h"
#include "ncview/frame_cache.h"
#include "ncview/frame_renderer.h"

class ViewerSession {
public:
	ViewerSession() = default;

	Dataset& dataset() { return dataset_; }
	const Dataset& dataset() const { return dataset_; }

	std::unique_ptr<ViewState>& activeView() { return view_; }
	const std::unique_ptr<ViewState>& activeView() const { return view_; }

	FrameCache& frameCache() { return frame_cache_; }
	const FrameCache& frameCache() const { return frame_cache_; }

	/* Builds FrameRenderer's settings from Options's render-shaped field
	 * group (transform, invert_colors, invert_physical, n_colors,
	 * n_extra_colors, display_type). Options itself is passed in rather
	 * than read from the global directly, so this stays testable without
	 * depending on global state. */
	PixelMapSettings pixelMapSettings( const Options &options ) const;

private:
	Dataset dataset_;
	std::unique_ptr<ViewState> view_;
	FrameCache frame_cache_;
};

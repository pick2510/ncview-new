/*
 * core/src/viewer_session.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * See ncview/viewer_session.h. OOP_redesign plan, Step 8.
 *
 * Scope note: the plan's ViewerSession section calls for the global
 * Options struct's ~50 fields to be split into four groups (render,
 * playback, session-lifetime, CLI/startup-only) and owned by ViewerSession.
 * Options is read directly from ~30 call sites across do_buttons.cc,
 * view.cc, viewer_controller.cc, and ui/main_window.cc (roughly 300+
 * individual options.<field> reads/writes in total) -- migrating that
 * storage wholesale in this step would be exactly the kind of large,
 * hard-to-verify mechanical rewrite that Step 7 explicitly avoided for the
 * interface.h/ViewerUi seam. This step instead: (a) makes ViewerSession the
 * real owner of Dataset/ViewState/FrameCache, the three pieces that were
 * cheap and low-risk to move (each already had a single well-defined
 * owner before this step -- g_dataset, view, framestore respectively; see
 * ncview.cc), and (b) migrates exactly one real Options consumer --
 * FrameRenderer's PixelMapSettings, previously built inline in util.cc's
 * data_to_pixels() -- onto ViewerSession::pixelMapSettings(). Options's
 * storage itself, and the rest of its field classification, remain future
 * work; the classification is recorded as grouped comments on the Options
 * struct in defines.h so it's visible without a second, unused struct
 * duplicating the field list.
 */
#include "ncview/viewer_session.h"

PixelMapSettings
ViewerSession::pixelMapSettings( const Options &options ) const
{
	return PixelMapSettings{
		options.transform, options.invert_colors != 0, options.invert_physical != 0,
		options.n_colors, options.n_extra_colors, options.display_type
	};
}

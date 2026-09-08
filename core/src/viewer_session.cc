/*
 * core/src/viewer_session.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * See ncview/viewer_session.h. OOP_redesign plan, Step 8/9a.
 *
 * Step 8 made ViewerSession the real owner of Dataset/ViewState/FrameCache
 * (each already had a single well-defined owner before that step --
 * g_dataset, view, framestore respectively; see ncview.cc) and migrated
 * one real Options consumer, FrameRenderer's PixelMapSettings, onto
 * ViewerSession::pixelMapSettings() below.
 *
 * Step 9a finishes the Options side: every field of the global Options
 * struct (defines.h) is now a reference member bound, in the constructor
 * defined here, to a field of one of the four grouped structs below
 * (RenderSettings, PlaybackSettings, SessionDisplayPrefs, StartupSettings
 * -- see viewer_session.h). Options's storage genuinely lives on
 * ViewerSession now; the ~300+ existing `options.<field>` call sites
 * across core/, ui/, and tests/ read/write through these references
 * unchanged.
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

Options::Options( ViewerSession &session ) :
	invert_physical		( session.renderSettings().invert_physical ),
	invert_colors		( session.renderSettings().invert_colors ),
	t_conv			( session.startupSettings().t_conv ),
	debug			( session.startupSettings().debug ),
	show_sel		( session.startupSettings().show_sel ),
	no_autoflip		( session.startupSettings().no_autoflip ),
	no_char_dims		( session.startupSettings().no_char_dims ),
	private_colormap	( session.startupSettings().private_colormap ),
	want_extra_info		( session.startupSettings().want_extra_info ),
	n_colors		( session.renderSettings().n_colors ),
	n_extra_colors		( session.renderSettings().n_extra_colors ),
	small			( session.startupSettings().small ),
	dump_frames		( session.startupSettings().dump_frames ),
	no_1d_vars		( session.startupSettings().no_1d_vars ),
	delta_step		( session.playbackSettings().delta_step ),
	listsel_max		( session.startupSettings().listsel_max ),
	color_by_ndims		( session.startupSettings().color_by_ndims ),
	beep_on_restart		( session.playbackSettings().beep_on_restart ),
	stop_on_restart		( session.playbackSettings().stop_on_restart ),
	auto_overlay		( session.startupSettings().auto_overlay ),
	blowup			( session.renderSettings().blowup ),
	maxsize_pct		( session.startupSettings().maxsize_pct ),
	maxsize_width		( session.startupSettings().maxsize_width ),
	maxsize_height		( session.startupSettings().maxsize_height ),
	blowup_default_size	( session.startupSettings().blowup_default_size ),
	display_type		( session.renderSettings().display_type ),
	transform		( session.renderSettings().transform ),
	min_max_method		( session.renderSettings().min_max_method ),
	varsel_style		( session.startupSettings().varsel_style ),
	shrink_method		( session.renderSettings().shrink_method ),
	calendar		( session.sessionDisplayPrefs().calendar ),
	blowup_type		( session.renderSettings().blowup_type ),
	autoscale		( session.renderSettings().autoscale ),
	save_frames		( session.sessionDisplayPrefs().save_frames ),
	frame_delay		( session.playbackSettings().frame_delay ),
	enable_group_sel	( session.startupSettings().enable_group_sel ),
	missval_r		( session.sessionDisplayPrefs().missval_r ),
	missval_g		( session.sessionDisplayPrefs().missval_g ),
	missval_b		( session.sessionDisplayPrefs().missval_b ),
	scale			( session.sessionDisplayPrefs().scale ),
	offset			( session.sessionDisplayPrefs().offset ),
	overlay			( session.sessionDisplayPrefs().overlay )
{
}

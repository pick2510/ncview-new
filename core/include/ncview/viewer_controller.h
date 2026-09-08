/*
 * core/include/ncview/viewer_controller.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * Owns the 21 do_*() actions that used to live as free functions in
 * do_buttons.cc, plus the playback state (cur_button, previously a file
 * -static) that drives them. OOP_redesign plan, Step 7 (scoped down --
 * see the commit message this ships with for what was deliberately left
 * for a later pass: the full ViewerUi virtual-interface conversion of
 * ncview/interface.h is NOT part of this step).
 *
 * do_buttons.cc keeps the free-function names (do_range, do_pause, ...,
 * which_button_pressed, in_button_pressed, in_colormap_selected) as thin
 * one-line forwards to g_app.controller (ncview/app_context.h), so every
 * existing call site elsewhere in core/, ui/, and tests/ keeps compiling
 * and behaving identically -- only do_buttons.cc itself changed to move
 * the actual logic here.
 */
#pragma once

#include "ncview/defines.h"

class ViewerController {
public:
	Button whichButtonPressed() const { return cur_button_; }

	/* The former in_button_pressed() switch body. */
	void dispatch( Button button_id, Modifier modifier );

	void range( Modifier modifier );
	void dimset( Modifier modifier );
	void restart( Modifier modifier );
	void rewind( Modifier modifier );
	void quit( Modifier modifier );
	void backwards( Modifier modifier );
	void pause( Modifier modifier );
	void forward( Modifier modifier );
	void fastforward( Modifier modifier );
	void colormapSelect( Modifier modifier );
	/* The former in_colormap_selected()'s body -- direct-pick counterpart
	 * of colormapSelect()'s cycle-by-one-step handling. */
	void colormapSelectByName( const char *name );
	void invertPhysical( Modifier modifier );
	void dataEdit( Modifier modifier );
	void invertColormap( Modifier modifier );
	/* Empty on purpose -- see do_buttons.cc's former do_set_minimum/
	 * do_set_maximum. Kept (rather than dropped, as the plan suggested)
	 * because Button::Minimum/Button::Maximum are still reachable via
	 * interface_fltk.cc's NCVIEW_TEST_BUTTON name table; removing these
	 * would turn that path's outcome from a no-op into dispatch()'s
	 * exit(-1) default case. */
	void setMinimum( Modifier modifier );
	void setMaximum( Modifier modifier );
	void blowup( Modifier modifier );
	void transform( Modifier modifier );
	void blowupType( Modifier modifier );
	void info( Modifier modifier );
	/* Named optionsDialog, not options -- a member named `options` would
	 * shadow the global `extern Options options;` for unqualified lookup
	 * inside this class's other methods. */
	void optionsDialog( Modifier modifier );
	/* Delegates to do_print() (core/src/do_print.cc), which stays a free
	 * function -- its printing logic is a large, separate subsystem that
	 * this scoped-down step doesn't move wholesale. */
	void print();

private:
	Button cur_button_ = Button::Pause;
};

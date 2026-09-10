/*
 * core/include/ncview/app_context.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * The single composition-root global. Earlier OOP_redesign passes replaced
 * ncview's original 5 free-standing globals (options, variables,
 * pixel_transform, framestore, view) with references bridged onto three
 * separately-named singletons (g_viewer_session, g_viewer_controller,
 * g_viewer_ui). Free-function callback seams (interface.h's in_/x_-prefixed
 * contract, do_buttons.cc's do_() actions) are called from many places
 * throughout core/ and ui/ with fixed signatures upstream defined -- a
 * fixed-signature free function can only reach an object instance through
 * global or static state, so eliminating application-level globals
 * entirely is not achievable without rewriting that seam itself (a much
 * larger, separate undertaking). What IS achievable, and what this does:
 * collapse three differently-named globals down to one, so there is a
 * single, clearly-named composition root instead of several pretending to
 * be independently-owned pieces of state. See PORTING.md.
 */
#pragma once

#include "ncview/viewer_controller.h"
#include "ncview/viewer_session.h"
#include "ncview/viewer_ui.h"

struct AppContext {
	/* "Refine the architecture" plan, Phase 2 gave ViewerController a
	 * real `ViewerSession &`, which means it can no longer be default
	 * constructed -- AppContext needs an explicit constructor to bind it
	 * to `session` below (declaration order controls member-init order,
	 * so session must stay declared first). This makes AppContext no
	 * longer an aggregate; nothing brace-initializes it (grepped for
	 * `AppContext{`/`AppContext {` across the tree -- the only two uses
	 * are `AppContext g_app;`, ncview.cc, and the `extern` declarations
	 * of it), so that costs nothing. */
	ViewerSession    session;
	ViewerController controller;
	/* Non-owning: whoever constructs the real ViewerUi (main.cc for the
	 * app, tests/stub_interface.cc for tests) owns its lifetime and
	 * points this at it before anything reaches the interface.h seam.
	 * Stays a nullable pointer set after the fact, not a reference bound
	 * at construction -- Phase 11c looked at making it a real reference
	 * and found it can't be, for the same reason `controller` can't hold
	 * one either (see viewer_controller.h): `g_app` itself has static
	 * storage duration and is fully constructed before any ViewerUi
	 * implementation exists to bind to. Only Phase 11f's restructuring
	 * (building the composition root once, in order, inside main())
	 * removes that constraint. */
	ViewerUi        *ui = nullptr;

	AppContext() : controller( session ) {}
};

/* Defined once, in ncview.cc. */
extern AppContext g_app;

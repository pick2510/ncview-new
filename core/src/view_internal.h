/*
 * core/src/view_internal.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * Private to core/src -- NOT part of the public core API (never included
 * from protos.h, never installed). "Refine the architecture" plan,
 * Phase 3e moved ViewerController's/ViewerSession's method bodies out of
 * view.cc into viewer_controller.cc/viewer_session.cc, the files named
 * after their classes. A handful of view.cc-local helpers are called
 * from both the moved code and code that stayed in view.cc (other
 * View:: methods, or -- for invalidate_variable/mouse_xy_to_data_xy/
 * view_data_edit_warn -- both); this header is the seam that lets them
 * stay un-exported to the rest of core/ while still crossing that one
 * new TU boundary. Include after ncview/protos.h in any .cc that needs
 * it.
 */
#pragma once

struct NCVar;

/* Definition (still `int`, not `static`) stays in view.cc, alongside
 * View::changeDat -- its other caller. Guards ViewerController::draw()
 * against re-entrancy from a modal dialog popped up mid-draw. */
extern int lockout_view_changes;

/* Definition stays in view.cc (set_scan_variable() and View::changeDat
 * are its other two callers); ViewerController::draw() is the third. */
void invalidate_variable( NCVar *var );

/* Definition stays in view.cc (View::setDataeditPlace() is its other
 * caller); ViewerController::reportPosition()/setMinFromCurdata()/
 * setMaxFromCurdata()/plotXY() are the rest. */
void mouse_xy_to_data_xy( int mouse_x, int mouse_y, int blowup, size_t *data_x, size_t *data_y );

/* Definition stays in view.cc (View::setAxis() is its other caller);
 * ViewerController::changeCurDim()/setCurDimIndex() are the rest. */
void view_data_edit_warn();

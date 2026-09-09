// Copyright (C) 2026 Dominik Strebel
//
// Headless implementation of the ncview/interface.h seam, as a
// RecordingViewerUi (ncview/viewer_ui.h). Proves ncview_core has no hidden
// UI dependency: if this file plus ncview_core links (see
// tests/CMakeLists.txt, which force-links the whole archive), the seam is
// clean. Every method here is a minimal, non-interactive stand-in -- never
// called from a real UI, only from tests.
//
// OOP_redesign plan, Step 9b: this used to be ~48 free functions directly
// satisfying ncview/interface.h's declarations; those declarations are now
// satisfied once, by core/src/viewer_ui_bridge.cc's forwarders onto
// whichever ViewerUi is currently installed (g_app.ui). This file's job
// is now to provide that ViewerUi for test binaries: g_recording_ui is a
// single global RecordingViewerUi instance, and g_app.ui is pointed at
// it via a namespace-scope initializer below, before any test's main()
// runs.
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "ncview/viewer_ui.h"

// --- Call recording + scripted dialog responses -----------------------
// Added for the OOP_redesign migration (see the plan's Step 1): later
// steps move workflows currently spread across do_buttons.cc/view.cc into
// a ViewerController, and need a way to characterize "what did core ask
// the UI to do, and in what order" (variable selection, playback) plus
// "what happens when the user cancels a dialog" (range/print/dimset)
// *before* moving that logic, as a regression net. Every method below
// appends its own name (and, where it helps distinguish calls, one
// human-readable argument) to g_recorded_calls; the handful of
// dialog-shaped methods return a caller-settable response instead of
// always succeeding. resetStubRecording() clears all of this back to its
// default (every dialog answers as if nothing was cancelled) -- call it
// at the start of any test that inspects g_recorded_calls or overrides a
// response, since doctest runs every TEST_CASE in the same process.
std::vector<std::string> g_recorded_calls;
Message g_dialog_response = Message::OK;
Message g_range_response = Message::OK;
Message g_printer_options_response = Message::OK;
int g_set_scan_dims_response = 0;

// --- Fake timer queue ---------------------------------------------------
// "Refine the architecture" plan, Phase 0b: in_timer_set() used to just
// record its own name and drop the callback, which is why playback
// (do_buttons.cc's rewind()/fastforward(), whose Modifier::M1 paths only
// ever advance by re-arming this same one-shot slot from inside their own
// callback -- see view.cc's comment on the shared timer slot) and the
// file-growth poll (view_check_new_data()) had zero test coverage: there
// was no way to actually fire the callback core handed over. Upstream's
// real interface.h contract is a genuine one-shot timer (the callback
// must re-arm itself via a fresh in_timer_set() call if it wants to run
// again, exactly like an X/FLTK timeout) -- fireTimer() reproduces that:
// it takes ownership of the pending callback and clears the "armed" flag
// *before* invoking it, so a callback that calls in_timer_set() again
// (as every real one does) correctly re-arms a fresh timer rather than
// stepping on the one being fired.
std::function<void()> g_pending_timer_callback;
unsigned long g_pending_timer_delay_ms = 0;
bool g_timer_armed = false;

void resetStubRecording()
{
	g_recorded_calls.clear();
	g_dialog_response = Message::OK;
	g_range_response = Message::OK;
	g_printer_options_response = Message::OK;
	g_set_scan_dims_response = 0;
	g_pending_timer_callback = nullptr;
	g_pending_timer_delay_ms = 0;
	g_timer_armed = false;
}

bool timerIsArmed() { return g_timer_armed; }
unsigned long timerDelayMs() { return g_pending_timer_delay_ms; }

// Fires the pending timer, if one is armed; a no-op (returns false)
// otherwise -- callers that don't know whether a timer is pending (e.g.
// a test loop simulating several seconds passing) should check the
// return value rather than assuming REQUIRE(timerIsArmed()) beforehand.
bool fireTimer()
{
	if (!g_timer_armed) return false;
	std::function<void()> callback = std::move(g_pending_timer_callback);
	g_pending_timer_callback = nullptr;
	g_timer_armed = false;
	callback();
	return true;
}

// Captured for test_view_data_edit.cc: view_data_edit() builds this array
// and hands ownership to x_dataedit(), which upstream's real FLTK dialog
// consumes and frees once the user closes it. The stub can't reproduce
// that dialog, so it stashes the pointer/count here and the test frees it
// after inspecting the content -- this is also what let ASan's
// heap-buffer-overflow report (modernization.md's Phase 0d findings) point
// straight at view_data_edit()'s allocation instead of some later dialog code.
char **g_last_dataedit_lines = nullptr;
int g_last_dataedit_nx = 0;

class RecordingViewerUi : public ViewerUi {
public:
	void in_display_stuff(const char*, const char*) override { g_recorded_calls.push_back("in_display_stuff"); }
	void in_set_edit_place(size_t, int, int, int, int) override { g_recorded_calls.push_back("in_set_edit_place"); }
	void in_indicate_active_var(const char *name) override { g_recorded_calls.push_back(std::string("in_indicate_active_var:") + (name ? name : "")); }
	void in_indicate_active_dim(Dimension, const char*) override { g_recorded_calls.push_back("in_indicate_active_dim"); }
	void in_parse_args(int*, char**) override { g_recorded_calls.push_back("in_parse_args"); }
	void in_initialize() override { g_recorded_calls.push_back("in_initialize"); }
	void in_set_label(Label, const char *s) override { g_recorded_calls.push_back(std::string("in_set_label:") + (s ? s : "")); }
	void in_process_user_input() override { g_recorded_calls.push_back("in_process_user_input"); }
	void in_draw_2d_field(const unsigned char*, size_t, size_t, size_t) override { g_recorded_calls.push_back("in_draw_2d_field"); }
	void in_create_colormap(const char*, const ncv_pixel[256], const ncv_pixel[256], const ncv_pixel[256]) override { g_recorded_calls.push_back("in_create_colormap"); }
	char *in_install_next_colormap(int) override { g_recorded_calls.push_back("in_install_next_colormap"); return nullptr; }
	int in_set_2d_size(size_t, size_t) override { g_recorded_calls.push_back("in_set_2d_size"); return 0; }
	void in_set_sensitive(Button, int) override { g_recorded_calls.push_back("in_set_sensitive"); }
	Message in_dialog(const char*, int) override { g_recorded_calls.push_back("in_dialog"); return g_dialog_response; }
	void in_var_set_sensitive(const char*, int) override { g_recorded_calls.push_back("in_var_set_sensitive"); }
	void in_fill_dim_info(const NCDim*, int) override { g_recorded_calls.push_back("in_fill_dim_info"); }
	void in_set_cur_dim_value(const char*, const char*) override { g_recorded_calls.push_back("in_set_cur_dim_value"); }
	void in_set_cursor_busy() override { g_recorded_calls.push_back("in_set_cursor_busy"); }
	void in_set_cursor_normal() override { g_recorded_calls.push_back("in_set_cursor_normal"); }
	// Echoes back the current X/Y axes ("Y-axis first, then X-axis", per
	// View::setScanDims()'s own contract comment) as an "accept
	// unchanged" answer, rather than leaving *new_dim_list null: found
	// while writing test_button_dispatch.cc (Phase 1) that
	// setScanDims()'s cancel check (`scan_dims_result ==
	// static_cast<int>(Message::Cancel)`) is documented dead code -- an
	// upstream quirk where the real returned status is never actually
	// Message::Cancel's numeric value -- so it *always* falls through to
	// dereferencing *new_dim_list, unconditionally, regardless of what
	// this stub returns. A real UI's dialog always populates the list on
	// every path that survives that point; this stub needs to as well or
	// every call crashes.
	int in_set_scan_dims(const Stringlist*, const char *cur_x_name, const char *cur_y_name, Stringlist **new_dim_list) override {
		g_recorded_calls.push_back("in_set_scan_dims");
		if (new_dim_list) {
			*new_dim_list = nullptr;
			if (cur_y_name) stringlist_add_string(new_dim_list, cur_y_name);
			if (cur_x_name) stringlist_add_string(new_dim_list, cur_x_name);
		}
		return g_set_scan_dims_response;
	}
	void in_change_min(const char*) override { g_recorded_calls.push_back("in_change_min"); }
	void in_flush() override { g_recorded_calls.push_back("in_flush"); }
	int in_popup_XY_graph(size_t, int, double*, double*, const char*, const char*, const char*, const char*, const Stringlist*) override { g_recorded_calls.push_back("in_popup_XY_graph"); return 0; }
	void in_query_pointer_position(int *x, int *y) override { g_recorded_calls.push_back("in_query_pointer_position"); if (x) *x = 0; if (y) *y = 0; }
	void in_popup_2d_window() override { g_recorded_calls.push_back("in_popup_2d_window"); }
	void in_popdown_2d_window() override { g_recorded_calls.push_back("in_popdown_2d_window"); }
	void in_timer_clear() override {
		g_recorded_calls.push_back("in_timer_clear");
		g_pending_timer_callback = nullptr;
		g_timer_armed = false;
	}
	int in_report_auto_overlay() override { g_recorded_calls.push_back("in_report_auto_overlay"); return 0; }
	void in_timer_set(std::function<void()> callback, unsigned long delay_millisec) override {
		g_recorded_calls.push_back("in_timer_set");
		g_pending_timer_callback = std::move(callback);
		g_pending_timer_delay_ms = delay_millisec;
		g_timer_armed = true;
	}
	char *in_install_prev_colormap(int) override { g_recorded_calls.push_back("in_install_prev_colormap"); return nullptr; }
	char *in_install_colormap_by_name(const char*, int) override { g_recorded_calls.push_back("in_install_colormap_by_name"); return nullptr; }
	Stringlist *in_choose_input_files() override { g_recorded_calls.push_back("in_choose_input_files"); return nullptr; }
	Message in_choose_save_file(const char*, const char*, char*, size_t) override { g_recorded_calls.push_back("in_choose_save_file"); return Message::Cancel; }

	void set_options() override { g_recorded_calls.push_back("set_options"); }
	Message printer_options(PrintOptions*) override { g_recorded_calls.push_back("printer_options"); return g_printer_options_response; }
	void in_print(const PrintInfo&, const PrintOptions&) override { g_recorded_calls.push_back("in_print"); }
	Message x_range(float min, float max, float, float, float *ret_min, float *ret_max, int *allvars) override {
		g_recorded_calls.push_back("x_range");
		// Even on Message::OK, write through *ret_min/*ret_max/*allvars (as
		// "the user left the range unchanged") rather than leaving them
		// uninitialized -- a caller that only checks for Message::Cancel
		// before using them (as view_set_range() does) would otherwise read
		// garbage.
		if (ret_min) *ret_min = min;
		if (ret_max) *ret_max = max;
		if (allvars) *allvars = 0;
		return g_range_response;
	}
	void x_dataedit(char **text, int nx) override {
		g_recorded_calls.push_back("x_dataedit");
		g_last_dataedit_lines = text;
		g_last_dataedit_nx = nx;
	}
	int x_seen_colormap_name(const char*) override { g_recorded_calls.push_back("x_seen_colormap_name"); return 0; }
	void x_check_legal_colormap_loaded() override { g_recorded_calls.push_back("x_check_legal_colormap_loaded"); }
	void x_create_colorbar(float, float, Transform) override { g_recorded_calls.push_back("x_create_colorbar"); }
	void x_draw_colorbar() override { g_recorded_calls.push_back("x_draw_colorbar"); }
	void x_error(const char *message) override { g_recorded_calls.push_back("x_error"); std::fprintf(stderr, "ncview error: %s\n", message ? message : "(null)"); }
	void x_force_set_invert_state(int) override { g_recorded_calls.push_back("x_force_set_invert_state"); }
	void x_init_dim_info(const Stringlist*) override { g_recorded_calls.push_back("x_init_dim_info"); }
	void x_set_var_sensitivity(const char*, int) override { g_recorded_calls.push_back("x_set_var_sensitivity"); }
	void unlock_plot() override { g_recorded_calls.push_back("unlock_plot"); }
	Stringlist *get_persistent_X_state() override { g_recorded_calls.push_back("get_persistent_X_state"); return nullptr; }
	void pix_to_rgb(ncv_pixel pix, int *r, int *g, int *b) override { if (r) *r = pix; if (g) *g = pix; if (b) *b = pix; }
};

namespace {
RecordingViewerUi g_recording_ui;
} // namespace

// Called explicitly from each test binary's main() (tests/main.cc), NOT
// from a static initializer here: g_app (ncview/app_context.h) now
// contains a ViewerSession/ViewerController with non-trivial members
// (Dataset's vectors, etc.), so g_app itself requires dynamic
// initialization -- its constructor can run either before or after this
// TU's own static initializers, in unspecified order (the classic static-
// initialization-order fiasco). A static initializer here that set
// g_app.ui directly used to be safe back when g_viewer_ui was its own
// bare, constant-initialized pointer global; now that it's a member of a
// dynamically-initialized aggregate, setting it too early gets silently
// overwritten when g_app's own constructor subsequently runs and
// default-member-initializes ui back to nullptr. main() is guaranteed to
// run after all static initialization completes, so calling this from
// there sidesteps the ordering question entirely.
void installRecordingViewerUi()
{
	g_app.ui = &g_recording_ui;
}

// Copyright (C) 2026 Dominik Strebel
//
// Headless implementation of the ncview/interface.h seam. Proves
// ncview_core has no hidden UI dependency: if this file plus ncview_core
// links (see tests/CMakeLists.txt, which force-links the whole archive),
// the seam is clean. Every function here is a minimal, non-interactive
// stand-in -- never called from a real UI, only from tests.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

// --- Call recording + scripted dialog responses -----------------------
// Added for the OOP_redesign migration (see the plan's Step 1): later
// steps move workflows currently spread across do_buttons.cc/view.cc into
// a ViewerController, and need a way to characterize "what did core ask
// the UI to do, and in what order" (variable selection, playback) plus
// "what happens when the user cancels a dialog" (range/print/dimset)
// *before* moving that logic, as a regression net. Every stub function
// below appends its own name (and, where it helps distinguish calls, one
// human-readable argument) to g_recorded_calls; the handful of
// dialog-shaped functions return a caller-settable response instead of
// always succeeding. resetStubRecording() clears all of this back to its
// default (every dialog answers as if nothing was cancelled) -- call it
// at the start of any test that inspects g_recorded_calls or overrides a
// response, since doctest runs every TEST_CASE in the same process.
std::vector<std::string> g_recorded_calls;
Message g_dialog_response = Message::OK;
Message g_range_response = Message::OK;
Message g_printer_options_response = Message::OK;
int g_set_scan_dims_response = 0;

void resetStubRecording()
{
	g_recorded_calls.clear();
	g_dialog_response = Message::OK;
	g_range_response = Message::OK;
	g_printer_options_response = Message::OK;
	g_set_scan_dims_response = 0;
}

void in_display_stuff(const char*, const char*) { g_recorded_calls.push_back("in_display_stuff"); }
void in_set_edit_place(size_t, int, int, int, int) { g_recorded_calls.push_back("in_set_edit_place"); }
void in_indicate_active_var(const char *name) { g_recorded_calls.push_back(std::string("in_indicate_active_var:") + (name ? name : "")); }
void in_indicate_active_dim(Dimension, const char*) { g_recorded_calls.push_back("in_indicate_active_dim"); }
void in_parse_args(int*, char**) { g_recorded_calls.push_back("in_parse_args"); }
void in_initialize() { g_recorded_calls.push_back("in_initialize"); }
void in_set_label(Label, const char *s) { g_recorded_calls.push_back(std::string("in_set_label:") + (s ? s : "")); }
void in_process_user_input() { g_recorded_calls.push_back("in_process_user_input"); }
void in_draw_2d_field(const unsigned char*, size_t, size_t, size_t) { g_recorded_calls.push_back("in_draw_2d_field"); }
void in_create_colormap(const char*, const ncv_pixel[256], const ncv_pixel[256], const ncv_pixel[256]) { g_recorded_calls.push_back("in_create_colormap"); }
char *in_install_next_colormap(int) { g_recorded_calls.push_back("in_install_next_colormap"); return nullptr; }
int in_set_2d_size(size_t, size_t) { g_recorded_calls.push_back("in_set_2d_size"); return 0; }
void in_set_sensitive(Button, int) { g_recorded_calls.push_back("in_set_sensitive"); }
Message in_dialog(const char*, int) { g_recorded_calls.push_back("in_dialog"); return g_dialog_response; }
void in_var_set_sensitive(const char*, int) { g_recorded_calls.push_back("in_var_set_sensitive"); }
void in_fill_dim_info(const NCDim*, int) { g_recorded_calls.push_back("in_fill_dim_info"); }
void in_set_cur_dim_value(const char*, const char*) { g_recorded_calls.push_back("in_set_cur_dim_value"); }
void in_set_cursor_busy() { g_recorded_calls.push_back("in_set_cursor_busy"); }
void in_set_cursor_normal() { g_recorded_calls.push_back("in_set_cursor_normal"); }
int in_set_scan_dims(const Stringlist*, const char*, const char*, Stringlist **new_dim_list) { g_recorded_calls.push_back("in_set_scan_dims"); if (new_dim_list) *new_dim_list = nullptr; return g_set_scan_dims_response; }
void in_change_min(const char*) { g_recorded_calls.push_back("in_change_min"); }
void in_flush() { g_recorded_calls.push_back("in_flush"); }
int in_popup_XY_graph(size_t, int, double*, double*, const char*, const char*, const char*, const char*, const Stringlist*) { g_recorded_calls.push_back("in_popup_XY_graph"); return 0; }
void in_query_pointer_position(int *x, int *y) { g_recorded_calls.push_back("in_query_pointer_position"); if (x) *x = 0; if (y) *y = 0; }
void in_popup_2d_window() { g_recorded_calls.push_back("in_popup_2d_window"); }
void in_popdown_2d_window() { g_recorded_calls.push_back("in_popdown_2d_window"); }
void in_timer_clear() { g_recorded_calls.push_back("in_timer_clear"); }
int in_report_auto_overlay() { g_recorded_calls.push_back("in_report_auto_overlay"); return 0; }
void in_timer_set(std::function<void()>, unsigned long) { g_recorded_calls.push_back("in_timer_set"); }
char *in_install_prev_colormap(int) { g_recorded_calls.push_back("in_install_prev_colormap"); return nullptr; }
char *in_install_colormap_by_name(const char*, int) { g_recorded_calls.push_back("in_install_colormap_by_name"); return nullptr; }
Stringlist *in_choose_input_files() { g_recorded_calls.push_back("in_choose_input_files"); return nullptr; }
Message in_choose_save_file(const char*, const char*, char*, size_t) { g_recorded_calls.push_back("in_choose_save_file"); return Message::Cancel; }

void set_options() { g_recorded_calls.push_back("set_options"); }
Message printer_options(PrintOptions*) { g_recorded_calls.push_back("printer_options"); return g_printer_options_response; }
void in_print(const PrintInfo&, const PrintOptions&) { g_recorded_calls.push_back("in_print"); }
Message x_range(float min, float max, float, float, float *ret_min, float *ret_max, int *allvars) {
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
// Captured for test_view_data_edit.cc: view_data_edit() builds this array
// and hands ownership to x_dataedit(), which upstream's real FLTK dialog
// consumes and frees once the user closes it. The stub can't reproduce
// that dialog, so it stashes the pointer/count here and the test frees it
// after inspecting the content -- this is also what let ASan's
// heap-buffer-overflow report (modernization.md's Phase 0d findings) point
// straight at view_data_edit()'s allocation instead of some later dialog code.
char **g_last_dataedit_lines = nullptr;
int g_last_dataedit_nx = 0;
void x_dataedit(char **text, int nx) {
    g_recorded_calls.push_back("x_dataedit");
    g_last_dataedit_lines = text;
    g_last_dataedit_nx = nx;
}
int x_seen_colormap_name(const char*) { g_recorded_calls.push_back("x_seen_colormap_name"); return 0; }
void x_check_legal_colormap_loaded() { g_recorded_calls.push_back("x_check_legal_colormap_loaded"); }
void x_create_colorbar(float, float, Transform) { g_recorded_calls.push_back("x_create_colorbar"); }
void x_draw_colorbar() { g_recorded_calls.push_back("x_draw_colorbar"); }
void x_error(const char *message) { g_recorded_calls.push_back("x_error"); std::fprintf(stderr, "ncview error: %s\n", message ? message : "(null)"); }
void x_force_set_invert_state(int) { g_recorded_calls.push_back("x_force_set_invert_state"); }
void x_init_dim_info(const Stringlist*) { g_recorded_calls.push_back("x_init_dim_info"); }
void x_set_var_sensitivity(const char*, int) { g_recorded_calls.push_back("x_set_var_sensitivity"); }
void unlock_plot() { g_recorded_calls.push_back("unlock_plot"); }
Stringlist *get_persistent_X_state() { g_recorded_calls.push_back("get_persistent_X_state"); return nullptr; }
void pix_to_rgb(ncv_pixel pix, int *r, int *g, int *b) { if (r) *r = pix; if (g) *g = pix; if (b) *b = pix; }

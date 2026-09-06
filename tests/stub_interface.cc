// Headless implementation of the ncview/interface.h seam. Proves
// ncview_core has no hidden UI dependency: if this file plus ncview_core
// links (see tests/CMakeLists.txt, which force-links the whole archive),
// the seam is clean. Every function here is a minimal, non-interactive
// stand-in -- never called from a real UI, only from tests.
#include <cstdio>
#include <cstring>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

void in_display_stuff(const char*, const char*) {}
void in_set_edit_place(size_t, int, int, int, int) {}
void in_indicate_active_var(const char*) {}
void in_indicate_active_dim(Dimension, const char*) {}
void in_parse_args(int*, char**) {}
void in_initialize() {}
void in_set_label(Label, const char*) {}
void in_process_user_input() {}
void in_draw_2d_field(const unsigned char*, size_t, size_t, size_t) {}
void in_create_colormap(const char*, const ncv_pixel[256], const ncv_pixel[256], const ncv_pixel[256]) {}
char *in_install_next_colormap(int) { return nullptr; }
int in_set_2d_size(size_t, size_t) { return 0; }
void in_set_sensitive(Button, int) {}
Message in_dialog(const char*, char *ret_string, int) { if (ret_string) ret_string[0] = '\0'; return Message::OK; }
void in_var_set_sensitive(const char*, int) {}
void in_fill_dim_info(const NCDim*, int) {}
void in_set_cur_dim_value(const char*, const char*) {}
void in_set_cursor_busy() {}
void in_set_cursor_normal() {}
int in_set_scan_dims(const Stringlist*, const char*, const char*, Stringlist **new_dim_list) { if (new_dim_list) *new_dim_list = nullptr; return 0; }
void in_change_min(const char*) {}
void in_flush() {}
int in_popup_XY_graph(size_t, int, double*, double*, const char*, const char*, const char*, const char*, const Stringlist*) { return 0; }
void in_query_pointer_position(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; }
void in_popup_2d_window() {}
void in_popdown_2d_window() {}
void in_timer_clear() {}
int in_report_auto_overlay() { return 0; }
void in_timer_set(std::function<void()>, unsigned long) {}
char *in_install_prev_colormap(int) { return nullptr; }
char *in_install_colormap_by_name(const char*, int) { return nullptr; }

void set_options() {}
Message printer_options(PrintOptions*) { return Message::OK; }
void printer_options_init() {}
Message x_range(float, float, float, float, float*, float*, int*) { return Message::OK; }
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
    g_last_dataedit_lines = text;
    g_last_dataedit_nx = nx;
}
int x_seen_colormap_name(const char*) { return 0; }
void x_check_legal_colormap_loaded() {}
void x_create_colorbar(float, float, Transform) {}
void x_draw_colorbar() {}
void x_error(const char *message) { std::fprintf(stderr, "ncview error: %s\n", message ? message : "(null)"); }
void x_force_set_invert_state(int) {}
void x_init_dim_info(const Stringlist*) {}
void x_set_var_sensitivity(const char*, int) {}
void unlock_plot() {}
Stringlist *get_persistent_X_state() { return nullptr; }
void pix_to_rgb(ncv_pixel pix, int *r, int *g, int *b) { if (r) *r = pix; if (g) *g = pix; if (b) *b = pix; }

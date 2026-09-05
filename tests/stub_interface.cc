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

void in_display_stuff(char*, char*) {}
void in_set_edit_place(size_t, int, int, int, int) {}
void in_indicate_active_var(char*) {}
void in_indicate_active_dim(int, char*) {}
void in_parse_args(int*, char**) {}
void in_initialize() {}
void in_set_label(int, char*) {}
void in_process_user_input() {}
void in_draw_2d_field(unsigned char*, size_t, size_t, size_t) {}
void in_create_colormap(char*, ncv_pixel[256], ncv_pixel[256], ncv_pixel[256]) {}
char *in_install_next_colormap(int) { return nullptr; }
int in_set_2d_size(size_t, size_t) { return 0; }
void in_set_sensitive(int, int) {}
int in_dialog(char*, char *ret_string, int) { if (ret_string) ret_string[0] = '\0'; return 0; }
void in_var_set_sensitive(char*, int) {}
void in_fill_dim_info(NCDim*, int) {}
void in_set_cur_dim_value(char*, char*) {}
void in_set_cursor_busy() {}
void in_set_cursor_normal() {}
int in_set_scan_dims(Stringlist*, char*, char*, Stringlist **new_dim_list) { if (new_dim_list) *new_dim_list = nullptr; return 0; }
void in_change_min(char*) {}
void in_flush() {}
int in_popup_XY_graph(size_t, int, double*, double*, char*, char*, char*, char*, Stringlist*) { return 0; }
void in_query_pointer_position(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; }
void in_popup_2d_window() {}
void in_popdown_2d_window() {}
void in_timer_clear() {}
int in_report_auto_overlay() { return 0; }
void in_timer_set(std::function<void()>, unsigned long) {}
char *in_install_prev_colormap(int) { return nullptr; }

void set_options() {}
int printer_options(PrintOptions*) { return 0; }
void printer_options_init() {}
int x_range(float, float, float, float, float*, float*, int*) { return 0; }
void x_dataedit(char**, int) {}
int x_seen_colormap_name(char*) { return 0; }
void x_check_legal_colormap_loaded() {}
void x_create_colorbar(float, float, int) {}
void x_draw_colorbar() {}
void x_error(char *message) { std::fprintf(stderr, "ncview error: %s\n", message ? message : "(null)"); }
void x_force_set_invert_state(int) {}
void x_init_dim_info(Stringlist*) {}
void x_set_var_sensitivity(char*, int) {}
void unlock_plot() {}
Stringlist *get_persistent_X_state() { return nullptr; }
void pix_to_rgb(ncv_pixel pix, int *r, int *g, int *b) { if (r) *r = pix; if (g) *g = pix; if (b) *b = pix; }

/*
 * core/src/interface_glue.cc
 *
 * A handful of functions from upstream's src/interface/interface.c that turn
 * out to have zero toolkit dependency: they are pure dispatch/bookkeeping
 * logic that happens to sit in the "interface" layer but is called from
 * BOTH core (util.cc calls in_button_pressed(); several core files call
 * in_error()) and the UI. Since core can't depend on ncview_ui, these live
 * here as real implementations rather than as stubs ncview_ui must provide.
 * Everything else upstream's interface.c did was a one-line forward to an
 * x_* function -- those stay genuine ncview_ui responsibilities, declared
 * in ncview/interface.h.
 */
#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

/*****************************************************************************
 * Vector through this routine when a new display variable has been
 * selected by the user, by pressing some sort of button.
 */
void
in_variable_selected( const char *var_name )
{
	NCVar	*var;

	if( (var = get_var( var_name )) == NULL ) {
		fprintf( stderr, "ncview: in_variable_selected: internal error " );
		fprintf( stderr, "no variable with name >%s< found on variable list\n",
					var_name );
		exit( -1 );
		}

	set_scan_variable( var );
}

/*****************************************************************************
 * Vector through this routine when a colormap has been picked directly (by
 * name) from the UI's colormap combobox -- the direct-pick counterpart of
 * do_colormap_sel()'s cycle-by-one-step BUTTON_COLORMAP_SELECT handling in
 * do_buttons.cc, which this deliberately mirrors (same in_install_..() then
 * view_draw()/view_recompute_colorbar() tail) so a picked colormap repaints
 * exactly like a cycled one does.
 */
void
in_colormap_selected( const char *name )
{
	in_install_colormap_by_name( name, true );
	view_draw( true, false );
	view_recompute_colorbar();
}

/*****************************************************************************
 * Called when a button is pressed (by the UI's widget callbacks, or by
 * core itself -- e.g. util.cc pauses playback on an error by calling
 * in_button_pressed(BUTTON_PAUSE, Modifier::M1)).  Argument 'button_id' indicates
 * which button was pressed.  Argument modifier should ideally take on one
 * of 4 values: Modifier::M1, Modifier::M2, Modifier::M3, and Modifier::M4, used in a generalized sense
 * to mean "normal action", "accelerated action", "backwards action", and
 * "accelerated backwards action". If these are not available, just always
 * use Modifier::M1.
 */
void
in_button_pressed( Button button_id, Modifier modifier )
{
	switch( button_id ) {
		case Button::Range:
			do_range( modifier );
			break;

		case Button::Dimset:
			do_dimset( modifier );
			break;

		case Button::Transform:
			do_transform( modifier );
			break;

		case Button::Blowup:
			do_blowup( modifier );
			break;

		case Button::Quit:
			do_quit( modifier );
			break;

		case Button::Restart:
			do_restart( modifier );
			break;

		case Button::Rewind:
			do_rewind( modifier );
			break;

		case Button::Backwards:
			do_backwards( modifier );
			break;

		case Button::Pause:
			do_pause( modifier );
			break;

		case Button::Forward:
			do_forward( modifier );
			break;

		case Button::Fastforward:
			do_fastforward( modifier );
			break;

		case Button::ColormapSelect:
			do_colormap_sel( modifier );
			break;

		case Button::InvertPhysical:
			do_invert_physical( modifier );
			break;

		case Button::InvertColormap:
			do_invert_colormap( modifier );
			break;

		case Button::Minimum:
			do_set_minimum( modifier );
			break;

		case Button::Maximum:
			do_set_maximum( modifier );
			break;

		case Button::BlowupType:
			do_blowup_type( modifier );
			break;

		case Button::Edit:
			do_data_edit( modifier );
			break;

		case Button::Info:
			do_info( modifier );
			break;

		case Button::Print:
			do_print();
			break;

		case Button::Options:
			do_options( modifier );
			break;

		default:
			fprintf( stderr, "in_button_pressed: unknown " );
			fprintf( stderr, "button id: %d\n", static_cast<int>(button_id) );
			exit( -1 );
		}
}

/*****************************************************************************
 * Indicate an error condition which can be continued from. Routed through
 * in_dialog (a real ncview_ui responsibility) rather than being one itself.
 */
void
in_error( const char *message )
{
	in_dialog( message, NULL, false );
}

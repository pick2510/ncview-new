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
in_variable_selected( char *var_name )
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
in_colormap_selected( char *name )
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
in_button_pressed( int button_id, Modifier modifier )
{
	switch( button_id ) {
		case BUTTON_RANGE:
			do_range( modifier );
			break;

		case BUTTON_DIMSET:
			do_dimset( modifier );
			break;

		case BUTTON_TRANSFORM:
			do_transform( modifier );
			break;

		case BUTTON_BLOWUP:
			do_blowup( modifier );
			break;

		case BUTTON_QUIT:
			do_quit( modifier );
			break;

		case BUTTON_RESTART:
			do_restart( modifier );
			break;

		case BUTTON_REWIND:
			do_rewind( modifier );
			break;

		case BUTTON_BACKWARDS:
			do_backwards( modifier );
			break;

		case BUTTON_PAUSE:
			do_pause( modifier );
			break;

		case BUTTON_FORWARD:
			do_forward( modifier );
			break;

		case BUTTON_FASTFORWARD:
			do_fastforward( modifier );
			break;

		case BUTTON_COLORMAP_SELECT:
			do_colormap_sel( modifier );
			break;

		case BUTTON_INVERT_PHYSICAL:
			do_invert_physical( modifier );
			break;

		case BUTTON_INVERT_COLORMAP:
			do_invert_colormap( modifier );
			break;

		case BUTTON_MINIMUM:
			do_set_minimum( modifier );
			break;

		case BUTTON_MAXIMUM:
			do_set_maximum( modifier );
			break;

		case BUTTON_BLOWUP_TYPE:
			do_blowup_type( modifier );
			break;

		case BUTTON_EDIT:
			do_data_edit( modifier );
			break;

		case BUTTON_INFO:
			do_info( modifier );
			break;

		case BUTTON_PRINT:
			do_print();
			break;

		case BUTTON_OPTIONS:
			do_options( modifier );
			break;

		default:
			fprintf( stderr, "in_button_pressed: unknown " );
			fprintf( stderr, "button id: %d\n", button_id  );
			exit( -1 );
		}
}

/*****************************************************************************
 * Indicate an error condition which can be continued from. Routed through
 * in_dialog (a real ncview_ui responsibility) rather than being one itself.
 */
void
in_error( char *message )
{
	in_dialog( message, NULL, false );
}

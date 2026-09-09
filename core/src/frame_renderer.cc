/*
 * core/src/frame_renderer.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * See ncview/frame_renderer.h. Arithmetic here must stay byte-for-byte
 * identical to the loop it was extracted from (data_to_pixels(),
 * core/src/util.cc) -- same clip bounds, same transform formulas, same
 * PseudoColor remap condition -- since tests/test_pixels.cc's existing
 * exact-pixel characterization tests, and tests/ui_smoke.sh's screenshot
 * goldens, both depend on the result matching precisely.
 */
#include "ncview/frame_renderer.h"

#include "ncview/includes.h"	/* FILL_FLOAT, an alias netcdf.h provides for NC_FILL_FLOAT */
#include "ncview/protos.h"	/* close_enough(), clip_f() */

void FrameRenderer::render(
	const float *scaled_data, size_t nx, size_t ny,
	float fill_value, float user_min, float user_max,
	const PixelMapSettings &settings,
	const std::vector<ncv_pixel> &pixel_transform,
	ncv_pixel *out_pixels )
{
	const float data_range = user_max - user_min;

	for( size_t j=0; j<ny; j++ ) {

		size_t j2;
		if( settings.invert_physical )
			j2 = j;
		else
			j2 = ny - j - 1;

		for( size_t i=0; i<nx; i++ ) {
			float rawdata = scaled_data[i + j2*nx];
			ncv_pixel pix_val;
			if( close_enough(rawdata, fill_value) || (rawdata == FILL_FLOAT))
				pix_val = pixel_transform[0];
			else
				{
				float data = (rawdata - user_min) / data_range;
				clip_f( &data, 0.0, .9999 );
				pix_val = (ncv_pixel)colorIndex<float>(
					data, settings.transform, settings.invert_colors,
					settings.n_colors, settings.n_extra_colors );
				if( settings.display_type == PseudoColor )
					pix_val = pixel_transform[pix_val];
				}
			out_pixels[i + j*nx] = pix_val;
			}
		}
}

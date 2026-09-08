/*
 * core/include/ncview/frame_renderer.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * Extracted from data_to_pixels() (core/src/util.cc) -- OOP_redesign plan,
 * Step 2. This covers exactly the per-pixel mapping loop: missing-value
 * check, normalize by range, clip, apply the display transform, optional
 * color inversion, quantize to a colormap index, and (for PseudoColor)
 * remap through the pixel_transform table. It is deliberately narrow:
 * blowup expand/contract, overlay application, and the degenerate-range
 * recursion/dialogs all stay in data_to_pixels() -- they involve UI calls,
 * recursion, and shared-state mutation that this class doesn't need and
 * shouldn't take on. FrameRenderer itself reads no globals and mutates
 * nothing it wasn't given -- everything it needs is a parameter.
 */
#pragma once

#include <cstddef>
#include <vector>

#include "ncview/defines.h"

/* The subset of Options that shapes the pixel-mapping loop -- see the
 * comment above data_to_pixels() in util.cc for how these fields are
 * actually used. */
struct PixelMapSettings {
	Transform	transform;
	bool	invert_colors;
	bool	invert_physical;
	int	n_colors;
	int	n_extra_colors;
	int	display_type;	/* compared against PseudoColor, defines.h */
};

class FrameRenderer {
public:
	/* scaled_data is nx*ny floats (already blowup-expanded/contracted by
	 * the caller); out_pixels must have room for nx*ny ncv_pixel entries.
	 * fill_value marks missing data (-> pixel_transform[0]). user_min/
	 * user_max define the normalization range (data_to_pixels() has
	 * already handled the case where they're equal before calling this).
	 * pixel_transform is the colormap index table; only consulted for a
	 * missing value, or for every pixel when display_type == PseudoColor.
	 */
	static void render(
		const float *scaled_data, size_t nx, size_t ny,
		float fill_value, float user_min, float user_max,
		const PixelMapSettings &settings,
		const std::vector<ncv_pixel> &pixel_transform,
		ncv_pixel *out_pixels );
};

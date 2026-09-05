/*
 * ncview core includes.
 *
 * UI-free replacement for upstream's ncview.includes.h: no X11/Xt/Xaw, no
 * SciPlot, no autotools-generated config.h. HAVE_UDUNITS2 is provided
 * unconditionally via the build system (see core/CMakeLists.txt) since this
 * port always builds against the vendored UDUNITS-2.
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>

#include <netcdf.h>

#include "ncview/stringlist.h"

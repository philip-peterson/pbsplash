/* Build the nanosvg implementation here, to build it only once and have other
 * C files build fast during development. */
#include <stdio.h>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

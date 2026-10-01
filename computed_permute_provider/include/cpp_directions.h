#ifndef CPP_DIRECTIONS_H
#define CPP_DIRECTIONS_H

/* Test/bench support, not part of the provider ABI. */

#include <stdint.h>

/* Load the first dims rows of a Joe-Kuo binary table (33 LE uint32 per row:
 * 32, V[0..31]). Returns 0 on success. */
int cpp_load_directions(const char *path, uint32_t dims, uint32_t (*v)[32]);

#endif

#define _POSIX_C_SOURCE 200112L
#include "private/asian_genuine_permute.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FIXINGS = 16, PATHS = 4096 };

static void *a64(size_t bytes) {
  void *p = NULL;
  if (posix_memalign(&p, 64, bytes))
    return NULL;
  memset(p, 0, bytes);
  return p;
}

int main(int argc, char **argv) {
  if (argc != 2 ||
      (strcmp(argv[1], "unseasoned") && strcmp(argv[1], "seasoned")))
    return 2;
  asian_genuine_route_t *routes = a64(FIXINGS * sizeof *routes);
  fragment_map_t *maps = a64(FIXINGS * sizeof *maps);
  asian_genuine_state_t *state = a64(sizeof *state);
  float *x = a64(64), *growth = a64(64);
  if (!routes || !maps || !state || !x || !growth)
    return 2;
  for (uint32_t lane = 0; lane < 16; ++lane) {
    x[lane] = 0.001f * (float)(lane + 1);
    growth[lane] = 1.0001f + 0.00001f * (float)lane;
  }
  for (uint32_t p = 0; p < PATHS; ++p)
    state->s[p] = 100;
  for (uint32_t k = 0; k < FIXINGS; ++k) {
    maps[k].dimension = k + 1;
    maps[k].pattern_count = 1;
    for (uint32_t lane = 0; lane < 16; ++lane)
      maps[k].patterns[0][lane] = lane;
    routes[k].x_base = x;
    routes[k].growth_base = growth;
    routes[k].map = &maps[k];
    const uint32_t denominator = !strcmp(argv[1], "seasoned") ? 32 : 16;
    const float weight = (float)(FIXINGS - k) / (float)denominator;
    memcpy(&routes[k].weight_bits, &weight, 4);
    routes[k].fixing_index = k;
  }
  asian_genuine_sql_dual_control_diag(routes, FIXINGS, state);
  printf("route_symbol mode=%s checksum=%.9g\n", argv[1],
         state->s[0] + state->q[0] + state->l[0]);
  free(growth);
  free(x);
  free(state);
  free(maps);
  free(routes);
  return 0;
}

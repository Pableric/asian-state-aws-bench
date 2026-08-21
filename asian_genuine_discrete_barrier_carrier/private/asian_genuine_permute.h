#ifndef ASIAN_GENUINE_PERMUTE_H
#define ASIAN_GENUINE_PERMUTE_H
#include <stdint.h>
#include "../tools/fragment_service/public_harness/fragment_map.h"
typedef struct __attribute__((aligned(32))){
 const float*x_base; const float*growth_base; const fragment_map_t*map;
 uint32_t weight_bits; uint32_t fixing_index;
} asian_genuine_route_t;
_Static_assert(sizeof(asian_genuine_route_t)==32,"hot route size");
typedef struct __attribute__((aligned(64))){float s[4096],q[4096],l[4096];} asian_genuine_state_t;
int asian_genuine_prepare_route(const uint32_t*const*source_words,
 uint32_t source_block_count,const float*const*x_blocks,
 const float*const*growth_blocks,const uint32_t target[4096],
 uint32_t fixing_index,uint32_t fixing_count,fragment_map_t*map,
 asian_genuine_route_t*route);
void asian_genuine_sql_variable_diag(const asian_genuine_route_t*routes,
 uint32_t fixing_count,asian_genuine_state_t*state);
void asian_genuine_sql_dual_control_diag(const asian_genuine_route_t*routes,
 uint32_t fixing_count,asian_genuine_state_t*state);
void asian_genuine_sql_growth_log5_diag(const asian_genuine_route_t*routes,
 uint32_t fixing_count,asian_genuine_state_t*state);
void asian_intel_point_major_sql_diag(const float*x,uint32_t fixing_count,
 const float*weights,asian_genuine_state_t*state);
void asian_intel_dimension_major_sql_diag(const float*x,const float*growth,
 uint32_t fixing_count,const float*weights,asian_genuine_state_t*state);
#endif

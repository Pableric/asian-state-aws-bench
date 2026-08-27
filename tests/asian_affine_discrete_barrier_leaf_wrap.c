#include "private/asian_affine_discrete_barrier_lifecycle_diag.h"

#include <stdint.h>

uint64_t asian_affine_barrier_test_leaf_invocations;

#define WRAP_LEAF(name) \
double __real_##name(const asian_affine_barrier_context_t *); \
double __wrap_##name(const asian_affine_barrier_context_t *context) \
{ \
    ++asian_affine_barrier_test_leaf_invocations; \
    return __real_##name(context); \
}

WRAP_LEAF(asian_affine_barrier_vanilla_call_interleaved_diag)
WRAP_LEAF(asian_affine_barrier_vanilla_put_interleaved_diag)
WRAP_LEAF(asian_affine_barrier_down_call_self_interleaved_diag)
WRAP_LEAF(asian_affine_barrier_down_put_self_interleaved_diag)
WRAP_LEAF(asian_affine_barrier_vanilla_call_grouped_diag)
WRAP_LEAF(asian_affine_barrier_vanilla_put_grouped_diag)
WRAP_LEAF(asian_affine_barrier_up_call_self_grouped_diag)
WRAP_LEAF(asian_affine_barrier_up_put_self_grouped_diag)

#undef WRAP_LEAF

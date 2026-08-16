#ifndef EUROPEAN_REDUCED_SETUP_H
#define EUROPEAN_REDUCED_SETUP_H

#define EUROPEAN_SETUP_INTERNAL __attribute__((visibility("hidden")))

EUROPEAN_SETUP_INTERNAL void european_build_reduced_tail_schedule(
    const float params[4],
    float* schedule
);

EUROPEAN_SETUP_INTERNAL void european_build_composed_normal_schedule(
    const float params[4],
    float* combined_c0,
    float* combined_c1
);

#ifdef EUROPEAN_SETUP_TEST_REFERENCE
void european_build_reduced_tail_schedule_scalar(
    const float params[4],
    float* schedule
);

void european_build_composed_normal_schedule_scalar(
    const float params[4],
    float* combined_c0,
    float* combined_c1
);
#endif

#endif

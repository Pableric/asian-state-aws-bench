#ifndef ASIAN_KERNELS_H
#define ASIAN_KERNELS_H
#include "asian_pricer.h"
double asian_kernel_rank1(const asian_price_request_t* req);
double asian_kernel_pair(const asian_price_request_t* req);
double asian_kernel_final_z(const asian_price_request_t* req);
#endif

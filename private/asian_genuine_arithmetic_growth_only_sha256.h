#ifndef ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_SHA256_H
#define ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_SHA256_H

#include <stddef.h>

int asian_genuine_arithmetic_growth_only_file_sha256(const char *path,
                                                     char output[65]);
int asian_genuine_arithmetic_growth_only_self_sha256(char output[65]);
int asian_genuine_arithmetic_growth_only_memory_sha256(const void *data,
                                                       size_t bytes,
                                                       char output[65]);

#endif

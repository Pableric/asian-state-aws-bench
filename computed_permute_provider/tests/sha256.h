#ifndef CPP_SHA256_H
#define CPP_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t buf[64];
    uint32_t fill;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t n);
void sha256_hex(sha256_ctx *c, char out[65]);

#endif

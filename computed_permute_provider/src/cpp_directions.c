#include "cpp_directions.h"

#include <stdio.h>

int cpp_load_directions(const char *path, uint32_t dims, uint32_t (*v)[32])
{
    FILE *f = fopen(path, "rb");
    uint32_t d;
    if (f == NULL) {
        return -1;
    }
    for (d = 0; d < dims; ++d) {
        uint32_t row[33];
        uint32_t j;
        if (fread(row, sizeof(uint32_t), 33, f) != 33u || row[0] != 32u) {
            fclose(f);
            return -1;
        }
        for (j = 0; j < 32u; ++j) {
            v[d][j] = row[1u + j];
        }
    }
    fclose(f);
    return 0;
}

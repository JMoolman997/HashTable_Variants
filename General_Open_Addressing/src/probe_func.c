#include <stdint.h>     
#include <stdlib.h>
#include <basic_func.h>

/* Example linear and quadratic probe functions */
uint32_t linear_probe_func(uint32_t k, uint32_t i, uint32_t m) {
    return (k + i) & (m - 1);
}

uint32_t quadratic_probe_func(uint32_t k, uint32_t i, uint32_t m) {
    // Basic example: (k + i^2) mod m
    return (k + i * i) & (m - 1);//(k + i * i) % m;
}

// only covers entire m when m power of 2
uint32_t double_hash_probe_func(uint32_t k, uint32_t i, uint32_t m) {
    uint32_t h1, h2;
    h1 = k;
    h2 = (k << 1) | 1;
    return (h1 + i*h2) & (m - 1);

}

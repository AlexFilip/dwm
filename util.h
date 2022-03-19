/* See LICENSE file for copyright and license details. */

#include <stdint.h>

#define Maximum(A, B)               ((A) > (B) ? (A) : (B))
#define Minimum(A, B)               ((A) < (B) ? (A) : (B))
#define Between(Val, Min, Max)  (((uint64_t)((Val) - (Min))) <= ((Max) - (Min)))

void die(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);

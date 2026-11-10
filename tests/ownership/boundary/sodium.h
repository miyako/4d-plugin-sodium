#ifndef OWNERSHIP_SYNTHETIC_SODIUM_H
#define OWNERSHIP_SYNTHETIC_SODIUM_H

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <sodium/core.h>
#include <sodium/utils.h>
#include <sodium/crypto_pwhash.h>

// This ownership test must never invoke real cryptography or obtain entropy.
inline int sodium_init() { return 0; }
inline void sodium_memzero(void *p, size_t n) { std::memset(p, 0, n); }
inline int crypto_pwhash_str_alg(char *, const char *, unsigned long long,
                                unsigned long long, size_t, int) { std::abort(); }
inline int crypto_pwhash_str_verify(const char *, const char *,
                                   unsigned long long) { std::abort(); }
inline int crypto_pwhash_str_needs_rehash(const char *, unsigned long long,
                                         size_t) { std::abort(); }
#endif

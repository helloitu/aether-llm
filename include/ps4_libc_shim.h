
#ifndef PS4_LIBC_SHIM_H
#define PS4_LIBC_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

static inline int isascii(int __c) { return (__c & ~0x7f) == 0; }

#ifdef __cplusplus
}

#include <math.h>
#undef isfinite
#undef isnan
#undef isinf
#undef isnormal
#undef signbit

#define PS4_MATH_OVERLOADS(NAME, BUILTIN)                              \
    inline bool NAME(float x)       { return BUILTIN(x); }             \
    inline bool NAME(double x)      { return BUILTIN(x); }             \
    inline bool NAME(long double x) { return BUILTIN(x); }

PS4_MATH_OVERLOADS(isfinite, __builtin_isfinite)
PS4_MATH_OVERLOADS(isnan,    __builtin_isnan)
PS4_MATH_OVERLOADS(isinf,    __builtin_isinf)
PS4_MATH_OVERLOADS(isnormal, __builtin_isnormal)
PS4_MATH_OVERLOADS(signbit,  __builtin_signbit)

namespace std {
    PS4_MATH_OVERLOADS(isfinite, __builtin_isfinite)
    PS4_MATH_OVERLOADS(isnan,    __builtin_isnan)
    PS4_MATH_OVERLOADS(isinf,    __builtin_isinf)
    PS4_MATH_OVERLOADS(isnormal, __builtin_isnormal)
    PS4_MATH_OVERLOADS(signbit,  __builtin_signbit)
}

#endif

#endif

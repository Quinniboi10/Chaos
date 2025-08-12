#pragma once

#include "types.h"

#include <cstring>

// Based on Vine
namespace simd {
#if __x86_64__
#if defined(__AVX512F__)
constexpr usize ALIGNMENT = 64;
#elif defined(__AVX2__)
constexpr usize ALIGNMENT = 32;
#elif defined(__SSE__)
constexpr usize ALIGNMENT = 16;
#else
#error Unsupported CPU architecture. At least SEE is required.
#endif
#elif defined(__arm__) || defined(__aarch64__)
#if defined(__ARM_NEON)
constexpr usize ALIGNMENT = 16;
#else
constexpr usize ALIGNMENT = 0;
#endif
#else
#error Unsupported CPU architecture
#endif

constexpr usize VECTOR_BYTES = ALIGNMENT;

template<typename T>
constexpr usize VECTOR_SIZE = VECTOR_BYTES / sizeof(T);

template<typename T>
using Vector = T __attribute__((__vector_size__(VECTOR_BYTES)));

template<typename T>
inline Vector<T> load_ep(const void* data) {
    Vector<T> result;
    std::memcpy(&result, data, VECTOR_BYTES);
    return result;
}

template<typename Data, typename Output>
inline Vector<Output> load_ep(const void* data) {
    static_assert(sizeof(Data) <= sizeof(Output));
    Output vals[VECTOR_SIZE<Output>];

    const Data* src = static_cast<const Data*>(data);
    for (usize i = 0; i < VECTOR_SIZE<Output>; i++)
        vals[i] = static_cast<Output>(src[i]);

    Vector<Output> result;
    std::memcpy(&result, vals, VECTOR_BYTES);
    return result;
}

template<typename T>
inline Vector<T> add_ep(const Vector<T> a, const Vector<T> b) {
    return a + b;
}

template<typename T>
inline T reduce_ep(const Vector<T> v) {
    T vals[VECTOR_SIZE<T>];
    std::memcpy(vals, &v, sizeof(vals));
    T res = 0;
    for (T val : vals)
        res += val;
    return res;
}

#ifdef __x86_64__
#include <immintrin.h>
inline Vector<i32> madd_epi16(const Vector<i16> a, const Vector<i16> b) {
    #if defined(__AVX512F__)
    return _mm512_madd_epi16(a, b);
    #elif defined(__AVX2__)
    return _mm256_madd_epi16(a, b);
    #elif defined(__SSE__)
    return _mm_madd_epi16(a, b);
    #endif
}
#elif defined(__arm__) || defined(__aarch64__)
#if defined(__ARM_NEON)
#include <arm_neon.h>

inline Vector<i32> madd_epi16(const Vector<i16> a, const Vector<i16> b) {
    int32x4_t mul_low = vmull_s16(vget_low_s16(a), vget_low_s16(b));
    int32x4_t mul_high = vmull_s16(vget_high_s16(a), vget_high_s16(b));

    return vaddq_s32(mul_low, mul_high);
}

#endif
#endif
}
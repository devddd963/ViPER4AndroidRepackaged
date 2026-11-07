#ifndef NEON_MATH_H
#define NEON_MATH_H

#include <stdint.h>
#include <stddef.h>

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <emmintrin.h> // SSE2 intrinsics for x86 / x86_64
#endif

// =============================================================================
// Float32 -> Int16 SIMD Conversion with Hardware Saturation Clamping
// =============================================================================
static inline void float_to_s16_neon(const float* in, int16_t* out, size_t count) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    // ARM NEON: 4 samples per loop with saturation
    size_t i = 0;
    const float32x4_t scale = vdupq_n_f32(32767.0f);

    for (; i + 3 < count; i += 4) {
        float32x4_t f = vld1q_f32(in + i);
        float32x4_t scaled = vmulq_f32(f, scale);
        int32x4_t s32 = vcvtq_s32_f32(scaled);
        int16x4_t s16 = vqmovn_s32(s32);
        vst1_s16(out + i, s16);
    }

    for (; i < count; i++) {
        float v = in[i] * 32767.0f;
        if (v > 32767.0f) v = 32767.0f;
        else if (v < -32768.0f) v = -32768.0f;
        out[i] = (int16_t)v;
    }
#elif defined(__i386__) || defined(__x86_64__)
    // x86 / x86_64 SSE2: 8 samples per loop with hardware saturation (_mm_packs_epi32)
    size_t i = 0;
    const __m128 scale = _mm_set1_ps(32767.0f);

    for (; i + 7 < count; i += 8) {
        __m128 f0 = _mm_loadu_ps(in + i);
        __m128 f1 = _mm_loadu_ps(in + i + 4);
        __m128 s0 = _mm_mul_ps(f0, scale);
        __m128 s1 = _mm_mul_ps(f1, scale);
        __m128i i0 = _mm_cvtps_epi32(s0);
        __m128i i1 = _mm_cvtps_epi32(s1);
        __m128i packed = _mm_packs_epi32(i0, i1); // Hardware saturation clamp to Int16
        _mm_storeu_si128((__m128i*)(out + i), packed);
    }

    for (; i < count; i++) {
        float v = in[i] * 32767.0f;
        if (v > 32767.0f) v = 32767.0f;
        else if (v < -32768.0f) v = -32768.0f;
        out[i] = (int16_t)v;
    }
#else
    // Generic scalar fallback
    for (size_t i = 0; i < count; i++) {
        float v = in[i] * 32767.0f;
        if (v > 32767.0f) v = 32767.0f;
        else if (v < -32768.0f) v = -32768.0f;
        out[i] = (int16_t)v;
    }
#endif
}

// =============================================================================
// Int16 -> Float32 SIMD Conversion with WRITE and ACCUMULATE modes
// =============================================================================
static inline void s16_to_float_neon(const int16_t* in, float* out, size_t count, bool accumulate) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    // ARM NEON: 4 samples per loop
    size_t i = 0;
    const float32x4_t inv_scale = vdupq_n_f32(1.0f / 32768.0f);

    for (; i + 3 < count; i += 4) {
        int16x4_t s16 = vld1_s16(in + i);
        int32x4_t s32 = vmovl_s16(s16);
        float32x4_t f = vmulq_f32(vcvtq_f32_s32(s32), inv_scale);

        if (accumulate) {
            float32x4_t existing = vld1q_f32(out + i);
            f = vaddq_f32(existing, f);
        }
        vst1q_f32(out + i, f);
    }

    for (; i < count; i++) {
        float f = (float)in[i] * (1.0f / 32768.0f);
        if (accumulate) {
            out[i] += f;
        } else {
            out[i] = f;
        }
    }
#elif defined(__i386__) || defined(__x86_64__)
    // x86 / x86_64 SSE2: 4 samples per loop
    size_t i = 0;
    const __m128 inv_scale = _mm_set1_ps(1.0f / 32768.0f);

    for (; i + 3 < count; i += 4) {
        __m128i s16 = _mm_loadl_epi64((const __m128i*)(in + i));
        __m128i s32 = _mm_srai_epi32(_mm_unpacklo_epi16(s16, s16), 16);
        __m128 f = _mm_mul_ps(_mm_cvtepi32_ps(s32), inv_scale);

        if (accumulate) {
            __m128 existing = _mm_loadu_ps(out + i);
            f = _mm_add_ps(existing, f);
        }
        _mm_storeu_ps(out + i, f);
    }

    for (; i < count; i++) {
        float f = (float)in[i] * (1.0f / 32768.0f);
        if (accumulate) {
            out[i] += f;
        } else {
            out[i] = f;
        }
    }
#else
    // Generic scalar fallback
    for (size_t i = 0; i < count; i++) {
        float f = (float)in[i] * (1.0f / 32768.0f);
        if (accumulate) {
            out[i] += f;
        } else {
            out[i] = f;
        }
    }
#endif
}

#endif // NEON_MATH_H

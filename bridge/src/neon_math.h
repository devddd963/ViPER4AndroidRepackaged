#ifndef NEON_MATH_H
#define NEON_MATH_H

#include <stdint.h>
#include <stddef.h>

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

// =============================================================================
// Float32 -> Int16 SIMD Conversion with Hardware Saturation Clamping
// =============================================================================
static inline void float_to_s16_neon(const float* in, int16_t* out, size_t count) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    size_t i = 0;
    const float32x4_t scale = vdupq_n_f32(32767.0f);

    for (; i + 3 < count; i += 4) {
        float32x4_t f = vld1q_f32(in + i);
        float32x4_t scaled = vmulq_f32(f, scale);
        // Convert Float to Int32
        int32x4_t s32 = vcvtq_s32_f32(scaled);
        // Saturate Int32 to Int16 (clamps > 0dBFS peaks without wrapping)
        int16x4_t s16 = vqmovn_s32(s32);
        vst1_s16(out + i, s16);
    }

    // Scalar remainder loop
    for (; i < count; i++) {
        float v = in[i] * 32767.0f;
        if (v > 32767.0f) v = 32767.0f;
        else if (v < -32768.0f) v = -32768.0f;
        out[i] = (int16_t)v;
    }
#else
    // Fast scalar fallback for x86 / x86_64
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

    // Scalar remainder loop
    for (; i < count; i++) {
        float f = (float)in[i] * (1.0f / 32768.0f);
        if (accumulate) {
            out[i] += f;
        } else {
            out[i] = f;
        }
    }
#else
    // Fast scalar fallback for x86 / x86_64
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

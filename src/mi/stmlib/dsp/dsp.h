// Copyright 2012 Olivier Gillet.
//
// Author: Olivier Gillet (ol.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// DSP utility routines.

#ifndef STMLIB_UTILS_DSP_DSP_H_
#define STMLIB_UTILS_DSP_DSP_H_

#include "stmlib/stmlib.h"

#include <cmath>
#include <math.h>

namespace stmlib {

#define MAKE_INTEGRAL_FRACTIONAL(x) \
  int32_t x ## _integral = static_cast<int32_t>(x); \
  float x ## _fractional = x - static_cast<float>(x ## _integral);

inline float Interpolate(const float* table, float index, float size) {
  index *= size;
  MAKE_INTEGRAL_FRACTIONAL(index)
  float a = table[index_integral];
  // oWarps Parasites (Schwung port): an index exactly at a table's last entry
  // (Warps does this with its crossfade tables at 1.0, and with the bit
  // mangler's four-entry ops[] at 1.0) would read one entry past the end with
  // weight 0. Same value, without that read.
  if (index_fractional == 0.0f) return a;
  float b = table[index_integral + 1];
  return a + (b - a) * index_fractional;
}

// oWarps Parasites (Schwung port): float -> int32 the way the Cortex-M4 FPU
// (VCVT.S32.F32) and AArch64 (FCVTZS) both do it: round toward zero,
// saturate out-of-range values, NaN -> 0. C++ leaves the out-of-range and NaN
// cases undefined and x86 answers INT32_MIN for them, so the desktop builds
// take this explicit form; on ARM it is the plain conversion the firmware
// was compiled to.
inline int32_t ArmFloatToInt32(float x) {
#if defined(__arm__) || defined(__aarch64__)
  return static_cast<int32_t>(x);
#else
  if (x != x) return 0;
  if (x >= 2147483648.0f) return 2147483647;
  if (x <= -2147483648.0f) return -2147483647 - 1;
  return static_cast<int32_t>(x);
#endif
}

inline float InterpolateWrap(const float* table, float index, float size) {
  index -= static_cast<float>(static_cast<int32_t>(index));
  index *= size;
  MAKE_INTEGRAL_FRACTIONAL(index)
  float a = table[index_integral];
  float b = table[index_integral + 1];
  return a + (b - a) * index_fractional;
}

#define ONE_POLE(out, in, coefficient) out += (coefficient) * ((in) - out);
#define SLOPE(out, in, positive, negative) { \
  float error = (in) - out; \
  out += (error > 0 ? positive : negative) * error; \
}
#define SLEW(out, in, delta) { \
  float error = (in) - out; \
  float d = (delta); \
  if (error > d) { \
    error = d; \
  } else if (error < -d) { \
    error = -d; \
  } \
  out += error; \
}

inline float Crossfade(float a, float b, float fade) {
  return a + (b - a) * fade;
}

inline float SoftLimit(float x) {
  return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float SoftClip(float x) {
  if (x < -3.0f) {
    return -1.0f;
  } else if (x > 3.0f) {
    return 1.0f;
  } else {
    return SoftLimit(x);
  }
}

#ifdef TEST
  inline int32_t Clip16(int32_t x) {
    if (x < -32768) {
      return -32768;
    } else if (x > 32767) {
      return 32767;
    } else {
      return x;
    }
  }
  inline uint16_t ClipU16(int32_t x) {
    if (x < 0) {
      return 0;
    } else if (x > 65535) {
      return 65535;
    } else {
      return x;
    }
  }
#else
  inline int32_t Clip16(int32_t x) {
    int32_t result;
    __asm ("ssat %0, %1, %2" : "=r" (result) :  "I" (16), "r" (x) );
    return result;
  }
  inline uint32_t ClipU16(int32_t x) {
    uint32_t result;
    __asm ("usat %0, %1, %2" : "=r" (result) :  "I" (16), "r" (x) );
    return result;
  }
#endif
  
#ifdef TEST
  inline float Sqrt(float x) {
    return sqrtf(x);
  }
#else
  inline float Sqrt(float x) {
    float result;
    __asm ("vsqrt.f32 %0, %1" : "=w" (result) : "w" (x) );
    return result;
  }
#endif

inline int16_t SoftConvert(float x) {
  return Clip16(ArmFloatToInt32(SoftLimit(x * 0.5f) * 32768.0f));
}

}  // namespace stmlib

#endif  // STMLIB_UTILS_DSP_DSP_H_
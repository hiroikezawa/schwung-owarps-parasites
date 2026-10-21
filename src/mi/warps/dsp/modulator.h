// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
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
// Modulator.

#ifndef WARPS_DSP_MODULATOR_H_
#define WARPS_DSP_MODULATOR_H_

#include "stmlib/stmlib.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "warps/dsp/oscillator.h"
#include "warps/dsp/parameters.h"
#include "warps/dsp/quadrature_oscillator.h"
#include "warps/dsp/quadrature_transform.h"
#include "warps/dsp/sample_rate_converter.h"
#include "warps/dsp/vocoder.h"
#include "warps/resources.h"

namespace warps {

const size_t kMaxBlockSize = 96;
const size_t kOversampling = 6;
const size_t kLessOversampling = 4;
const size_t kNumOscillators = 1;

typedef struct { short l; short r; } ShortFrame;
typedef struct { float l; float r; } FloatFrame;

class SaturatingAmplifier {
 public:
  SaturatingAmplifier() { }
  ~SaturatingAmplifier() { }
  void Init() {
    drive_ = 0.0f;
  }
  
  void Process(
      float drive,
      float limit,
      short* in,
      float* out,
      float* out_raw,
      size_t in_stride,
      size_t size) {
    // Process noise gate and compute raw output
    stmlib::ParameterInterpolator drive_modulation(&drive_, drive, size);
    float level = level_;
    for (size_t i = 0; i < size; ++i) {
      float s = static_cast<float>(*in) / 32768.0f;
      float error = s * s - level;
      level += error * (error > 0.0f ? 0.1f: 0.0001f);
      s *= level <= 0.0001f ? (1.0f / 0.0001f) * level : 1.0f;
      out[i] = s;
      out_raw[i] += s * drive_modulation.Next();
      in += in_stride;
    }
    level_ = level;
    
    // Process overdrive / gain
    float drive_2 = drive * drive;
    float pre_gain_a = drive * 0.5f;
    float pre_gain_b = drive_2 * drive_2 * drive * 24.0f;
    float pre_gain = pre_gain_a + (pre_gain_b - pre_gain_a) * drive_2;
    float drive_squished = drive * (2.0f - drive);
    float post_gain = 1.0f / stmlib::SoftClip(
          0.33f + drive_squished * (pre_gain - 0.33f));
    stmlib::ParameterInterpolator pre_gain_modulation(
        &pre_gain_,
        pre_gain,
        size);
    stmlib::ParameterInterpolator post_gain_modulation(
        &post_gain_,
        post_gain,
        size);
    
    for (size_t i = 0; i < size; ++i) {
      float pre = pre_gain_modulation.Next() * out[i];
      float post = stmlib::SoftClip(pre) * post_gain_modulation.Next();
      out[i] = pre + (post - pre) * limit;
    }
  }

 private:
  float level_;
  float drive_;
  float post_gain_;
  float pre_gain_;
  float unclipped_gain_;

  DISALLOW_COPY_AND_ASSIGN(SaturatingAmplifier);
};

enum XmodAlgorithm {
  ALGORITHM_XFADE,
  ALGORITHM_FOLD,
  ALGORITHM_ANALOG_RING_MODULATION,
  ALGORITHM_DIGITAL_RING_MODULATION,
  ALGORITHM_RING_MODULATION,
  ALGORITHM_XOR,
  ALGORITHM_COMPARATOR,
  ALGORITHM_COMPARATOR8,
  ALGORITHM_CHEBYSCHEV,
  ALGORITHM_COMPARATOR_CHEBYSCHEV,
  ALGORITHM_BITCRUSHER,
  ALGORITHM_NOP,
  ALGORITHM_LAST
};

class Modulator {
 public:
  typedef void (Modulator::*XmodFn)(
      float balance,
      float balance_end,
      float parameter,
      float parameter_end,
      const float* in_1,
      const float* in_2,
      float* out,
      size_t size);

  Modulator() { }
  ~Modulator() { }

  void Init(float sample_rate);
  void Process(ShortFrame* input, ShortFrame* output, size_t size);
  template<XmodAlgorithm algorithm>
  void Process1(ShortFrame* input, ShortFrame* output, size_t size);
  void ProcessFreqShifter(ShortFrame* input, ShortFrame* output, size_t size);
  void ProcessVocoder(ShortFrame* input, ShortFrame* output, size_t size);
  void ProcessBitcrusher(ShortFrame* input, ShortFrame* output, size_t size);
  void ProcessDelay(ShortFrame* input, ShortFrame* output, size_t size);
  void ProcessDoppler(ShortFrame* input, ShortFrame* output, size_t size);
  void ProcessMeta(ShortFrame* input, ShortFrame* output, size_t size);
  inline Parameters* mutable_parameters() { return &parameters_; }
  inline const Parameters& parameters() { return parameters_; }
  
  inline bool bypass() const { return bypass_; }
  inline void set_bypass(bool bypass) { bypass_ = bypass; }

  // oWarps Parasites (Schwung port): the delay's head distance and tape speed
  // are smoothed from 0 at power-on (the distance with a ~0.5 s time
  // constant), so a module that has just been switched on starts with its
  // tape stopped and glides up to its delay time. A Schwung instance is
  // "switched on" at every set load; this starts both smoothers where the
  // knobs are instead (TIMBRE = head distance, ALGORITHM = speed, raw pot
  // positions 0..1), as though the module had been running.
  inline void PrimeDelay(float head_distance, float speed_pot) {
    delay_lp_time_ = head_distance * (DELAY_SIZE-10) + 5;
    float rate = speed_pot * 2.0f - 1.0f;
    delay_lp_rate_ = rate * rate * rate;
  }

  inline FeatureMode feature_mode() const { return feature_mode_; }
  inline void set_feature_mode(FeatureMode feature_mode) {
    // oWarps Parasites (Schwung port): feedback_sample_ lies in the storage
    // the delay and Doppler modes use as their delay line, so after either
    // of them it holds two audio samples read as a float - a NaN for many
    // quiet ones, which the frequency shifter's feedback then keeps for
    // ever (silence until power-off on the module). Entering the frequency
    // shifter starts its feedback from zero instead.
    if (feature_mode == FEATURE_MODE_FREQUENCY_SHIFTER &&
        feature_mode_ != FEATURE_MODE_FREQUENCY_SHIFTER) {
      feedback_sample_ = 0.0f;
    }
    feature_mode_ = feature_mode;
  }

 private:
  template<XmodAlgorithm algorithm_1, XmodAlgorithm algorithm_2>
  void ProcessXmod(
      float balance,
      float balance_end,
      float parameter,
      float parameter_end,
      const float* in_1,
      const float* in_2,
      float* out,
      size_t size) {
    float step = 1.0f / static_cast<float>(size);
    float parameter_increment = (parameter_end - parameter) * step;
    float balance_increment = (balance_end - balance) * step; 
    while (size) {
      {
        const float x_1 = *in_1++;
        const float x_2 = *in_2++;
        float a = Xmod<algorithm_1>(x_1, x_2, parameter);
        float b = Xmod<algorithm_2>(x_1, x_2, parameter);
        *out++ = a + (b - a) * balance;
        parameter += parameter_increment;
        balance += balance_increment;
        size--;
      }
      {
        const float x_1 = *in_1++;
        const float x_2 = *in_2++;
        float a = Xmod<algorithm_1>(x_1, x_2, parameter);
        float b = Xmod<algorithm_2>(x_1, x_2, parameter);
        *out++ = a + (b - a) * balance;
        parameter += parameter_increment;
        balance += balance_increment;
        size--;
      }
      {
        const float x_1 = *in_1++;
        const float x_2 = *in_2++;
        float a = Xmod<algorithm_1>(x_1, x_2, parameter);
        float b = Xmod<algorithm_2>(x_1, x_2, parameter);
        *out++ = a + (b - a) * balance;
        parameter += parameter_increment;
        balance += balance_increment;
        size--;
      }
    }
  }
  
  template<XmodAlgorithm algorithm>
  void ProcessXmod(
      float p_1,
      float p_1_end,
      float p_2,
      float p_2_end,
      const float* in_1,
      const float* in_2,
      float* out,
      size_t size) {
    float step = 1.0f / static_cast<float>(size);
    float p_1_increment = (p_1_end - p_1) * step;
    float p_2_increment = (p_2_end - p_2) * step;
    while (size) {
      const float x_1 = *in_1++;
      const float x_2 = *in_2++;
      *out++ = Xmod<algorithm>(x_1, x_2, p_1, p_2);
      p_1 += p_1_increment;
      p_2 += p_2_increment;
      size--;
    }
  }

  template<XmodAlgorithm algorithm>
  void ProcessXmod(
      float p_1,
      float p_1_end,
      float p_2,
      float p_2_end,
      const float* in_1,
      const float* in_2,
      float* out_1,
      float* out_2,
      size_t size) {
    float step = 1.0f / static_cast<float>(size);
    float p_1_increment = (p_1_end - p_1) * step;
    float p_2_increment = (p_2_end - p_2) * step;
    while (size) {
      const float x_1 = *in_1++;
      const float x_2 = *in_2++;
      *out_1++ = Xmod<algorithm>(x_1, x_2, p_1, p_2, out_2++); /* TODO error */
      p_1 += p_1_increment;
      p_2 += p_2_increment;
      size--;
    }
  }
  
  template<XmodAlgorithm algorithm>
  static float Xmod(float x_1, float x_2, float parameter);

  // oWarps Parasites (Schwung port): not static, so that the Chebyschev
  // envelope follower (formerly a function-local static) is per instance.
  template<XmodAlgorithm algorithm>
  float Xmod(float x_1, float x_2, float p_1, float p_2);

  template<XmodAlgorithm algorithm>
  static float Xmod(float x_1, float x_2, float p_1, float p_2, float *out_2);

  template<XmodAlgorithm algorithm>
  void ProcessMod(
      float p,
      float p_end,
      const float* in,
      float* out,
      size_t size) {
    float step = 1.0f / static_cast<float>(size);
    float p_increment = (p_end - p) * step;
    while (size) {
      const float x = *in++;
      *out++ = Mod<algorithm>(x, p);
      p += p_increment;
      size--;
    }
  }

  template<XmodAlgorithm algorithm>
  float Mod(float x, float p);

  static float Diode(float x);
  
  bool bypass_;

  FeatureMode feature_mode_;

  Parameters parameters_;
  Parameters previous_parameters_;
  
  SaturatingAmplifier amplifier_[2];

  Oscillator xmod_oscillator_;
  Oscillator vocoder_oscillator_;
  QuadratureOscillator quadrature_oscillator_;
  SampleRateConverter<SRC_UP, kOversampling, 48> src_up_[2];
  SampleRateConverter<SRC_DOWN, kOversampling, 48> src_down_;
  SampleRateConverter<SRC_UP, kLessOversampling, 48> src_up2_[2];
  SampleRateConverter<SRC_DOWN, kLessOversampling, 48> src_down2_[2];
  Vocoder vocoder_;
  QuadratureTransform quadrature_transform_[2];  

  stmlib::OnePole filter_[4];

  // oWarps Parasites (Schwung port): the former function-local statics of
  // ProcessDelay, ProcessDoppler and Mod<ALGORITHM_CHEBYSCHEV>.
  FloatFrame delay_feedback_sample_;
  int32_t delay_write_head_;
  float delay_write_position_;
  FloatFrame delay_previous_samples_[3];
  float delay_lp_time_;
  float delay_lp_rate_;
  size_t doppler_cursor_;
  float doppler_lfo_phase_;
  float doppler_distance_;
  float doppler_angle_;
  float chebyschev_envelope_;

  /* everything that follows will be used as delay buffer */
  // oWarps Parasites (Schwung port): the firmware indexes delay_buffer_ past
  // its end into the members declared after it. The same storage, shared
  // explicitly: delay_area_ spans exactly those members, in the same order.
  union {
  struct {
  ShortFrame delay_buffer_[8192+4096];  
  float internal_modulation_[kMaxBlockSize];
  float buffer_[3][kMaxBlockSize];
  float src_buffer_[2][kMaxBlockSize * kOversampling];
  float feedback_sample_;
  };
  ShortFrame delay_area_[(sizeof(ShortFrame) * (8192+4096)
                          + sizeof(float) * kMaxBlockSize
                          + sizeof(float) * 3 * kMaxBlockSize
                          + sizeof(float) * 2 * kMaxBlockSize * kOversampling
                          + sizeof(float)) / sizeof(ShortFrame)];
  };

  enum DelaySize {
    DELAY_SIZE = (sizeof(delay_buffer_)
                  + sizeof(internal_modulation_)
                  + sizeof(buffer_)
                  + sizeof(src_buffer_)
                  + sizeof(feedback_sample_)) / sizeof(ShortFrame) - 4
  };

  enum DelayInterpolation {
    INTERPOLATION_ZOH,
    INTERPOLATION_LINEAR,
    INTERPOLATION_HERMITE,
  };

  DelayInterpolation delay_interpolation_;

  static XmodFn xmod_table_[];

  DISALLOW_COPY_AND_ASSIGN(Modulator);
};

}  // namespace warps

#endif  // WARPS_DSP_MODULATOR_H_

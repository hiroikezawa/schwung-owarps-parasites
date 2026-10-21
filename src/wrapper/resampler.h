// oWarps Parasites - fixed-ratio polyphase resampler (44.1 kHz <-> Warps' 96 kHz).
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
//
// One output sample is a K-tap dot product with one of L coefficient phases
// (scripts/gen_tables.py). The phase advances by M per output and the input
// position by floor((p + M) / L): integer arithmetic only, so the long-term
// ratio is exact and nothing drifts over hours of playback.
//
// Used two ways:
//   input-driven  (44.1 -> 96 kHz):  Push(); while (Ready()) Next();
//   output-driven (96 -> 44.1 kHz):  while (!Ready()) Push(); Next();
// No allocation: the history lives inside the object.
#ifndef OWARPS_RESAMPLER_H_
#define OWARPS_RESAMPLER_H_

namespace owarps {

class Resampler {
 public:
  static const int kMaxTaps = 112;

  void Init(const float* coef, int L, int M, int K) {
    coef_ = coef;
    L_ = L;
    M_ = M;
    K_ = K;
    Reset();
  }

  void Reset() {
    for (int i = 0; i < 2 * kMaxTaps; ++i) hist_l_[i] = hist_r_[i] = 0.0f;
    w_ = 0;
    phase_ = 0;
    lag_ = 1;  // one input must arrive before the first output
  }

  // True when the next output can be computed from the inputs pushed so far.
  inline bool Ready() const { return lag_ <= 0; }

  inline void Push(float l, float r) {
    hist_l_[w_] = hist_l_[w_ + K_] = l;
    hist_r_[w_] = hist_r_[w_ + K_] = r;
    if (++w_ == K_) w_ = 0;
    --lag_;
  }

  // Computes the next output (Ready() must be true) and advances the phase.
  inline void Next(float* l, float* r) {
    const float* c = coef_ + phase_ * K_;
    const float* xl = hist_l_ + w_;   // oldest .. newest, contiguous
    const float* xr = hist_r_ + w_;
    float al0 = 0.0f, al1 = 0.0f, ar0 = 0.0f, ar1 = 0.0f;
    for (int k = 0; k < K_; k += 2) {
      al0 += c[k] * xl[k];
      ar0 += c[k] * xr[k];
      al1 += c[k + 1] * xl[k + 1];
      ar1 += c[k + 1] * xr[k + 1];
    }
    *l = al0 + al1;
    *r = ar0 + ar1;
    phase_ += M_;
    while (phase_ >= L_) {
      phase_ -= L_;
      ++lag_;
    }
  }

 private:
  const float* coef_;
  int L_, M_, K_;
  float hist_l_[2 * kMaxTaps];
  float hist_r_[2 * kMaxTaps];
  int w_;
  int phase_;
  int lag_;
};

}  // namespace owarps

#endif  // OWARPS_RESAMPLER_H_

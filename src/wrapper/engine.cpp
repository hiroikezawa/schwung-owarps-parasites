// oWarps Parasites - engine (see engine.h).
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#include "wrapper/engine.h"

#include <math.h>
#include <string.h>

#include "owarps_src_tables.h"

namespace owarps {

namespace {

const float kFadeStep = 1.0f / 480.0f;      // 5 ms at 96 kHz
const float kSmooth = 0.00226f;             // ~10 ms one-pole at 44.1 kHz
const float kDcPole = 0.999715f;            // 1 - 2 pi 2 Hz / 44100

inline float DbToGain(float db) { return powf(10.0f, db * 0.05f); }

inline short ToShort(float x) {
  float s = x * 32768.0f;
  if (s > 32767.0f) s = 32767.0f;
  if (s < -32768.0f) s = -32768.0f;
  return static_cast<short>(lrintf(s));
}

inline void Smooth(float* state, float target) {
  float d = target - *state;
  *state = (d > -1e-5f && d < 1e-5f) ? target : *state + kSmooth * d;
}

// Modes whose button selects the internal oscillator (the frequency
// shifter's selects its quadrature oscillator). Doppler: room size; delay:
// topology.
inline bool HasOscillator(int mode) {
  return mode != warps::FEATURE_MODE_DOPPLER && mode != warps::FEATURE_MODE_DELAY;
}

}  // namespace

bool Engine::Init(int host_rate) {
  if (host_rate != 44100) return false;
  for (int k = 0; k < 2; ++k) {
    modulator_[k].Init(96000.0f);   // warps.cc: modulator.Init(96000.0f)
    cv_[k].Init();
  }
  for (int i = 0; i < P_COUNT; ++i) params_[i] = kParams[i].def;
  up_.Init(src_up_coef, src_up_L, src_up_M, src_up_K);
  down_.Init(src_down_coef, src_down_L, src_down_M, src_down_K);
  midi_note_ = 60;
  has_note_ = false;
  test_transparent_ = false;
  nan_resets_ = 0;
  underruns_ = 0;
  dry_delay_ = kRoundTripSamples;
  Reset96k();
  ApplyConfig(Wanted());
  Snap();
  return true;
}

void Engine::Reset96k() {
  up_.Reset();
  down_.Reset();
  in_count_ = 0;
  out_read_ = 0;
  out_count_ = kSrcPrimeFrames;
  for (int i = 0; i < kFifoFrames; ++i) {
    in_l_[i] = in_r_[i] = out_l_[i] = out_r_[i] = 0.0f;
  }
  for (int i = 0; i < kWarpsBlock; ++i) loop_aux_[i] = 0;
  for (int i = 0; i < kMaxDryDelay; ++i) dry_l_[i] = dry_r_[i] = 0.0f;
  dc_x_[0] = dc_x_[1] = dc_y_[0] = dc_y_[1] = 0.0f;
  dry_write_ = 0;
  fade_ = 1.0f;
  fading_out_ = false;
}

int Engine::latency_samples() const { return dry_delay_; }

void Engine::SetParam(int index, float value) {
  if (index < 0 || index >= P_COUNT || !isfinite(value)) return;
  const ParamDef& d = kParams[index];
  if (d.type != TYPE_FLOAT) value = floorf(value + 0.5f);
  if (value < d.min) value = d.min;
  if (value > d.max) value = d.max;
  params_[index] = value;
}

Engine::Config Engine::Wanted() const {
  Config c;
  c.mode = static_cast<int>(params_[P_MODE]);
  c.routing = static_cast<int>(params_[P_ROUTING]);
  c.outputs = static_cast<int>(params_[P_OUTPUTS]);
  c.button = static_cast<int>(params_[kModeControls[c.mode][CTL_BUTTON]]);
  c.loop_patch = static_cast<int>(params_[P_LOOP_PATCH]);
  return c;
}

bool Engine::SameConfig(const Config& a, const Config& b) const {
  return a.mode == b.mode && a.routing == b.routing && a.outputs == b.outputs &&
         a.button == b.button && a.loop_patch == b.loop_patch;
}

bool Engine::OscillatorOn(const Config& c) const {
  return HasOscillator(c.mode) && c.button != 0;
}

// PRESERVE runs one Modulator per channel, except where the two inputs and
// two outputs ARE the stereo image: the Doppler panner (two sources, two
// ears) and the delay (two channels of tape).
bool Engine::UsesTwoModulators(const Config& c) const {
  return c.routing == ROUTE_PRESERVE && c.mode != warps::FEATURE_MODE_DOPPLER &&
         c.mode != warps::FEATURE_MODE_DELAY;
}

Pots Engine::PotsFor(int mode) const {
  const int* ctl = kModeControls[mode];
  float pot[4];
  for (int i = 0; i < 4; ++i) {
    const ParamDef& d = kParams[ctl[i]];
    pot[i] = (params_[ctl[i]] - d.min) / (d.max - d.min);
  }
  Pots p;
  p.algorithm = pot[CTL_ALGO];
  p.parameter = pot[CTL_PARAM];
  p.level_1 = pot[CTL_LEVEL1];
  p.level_2 = pot[CTL_LEVEL2];
  // 36..96 is the firmware's range for LEVEL 1 as a pitch pot (60 * pot + 36).
  p.note_pot = ctl[CTL_PITCH] >= 0 ? (params_[ctl[CTL_PITCH]] - 36.0f) / 60.0f : 0.4f;
  return p;
}

void Engine::ApplyConfig(const Config& c) {
  applied_ = c;
  Pots pots = PotsFor(c.mode);
  for (int k = 0; k < 2; ++k) {
    modulator_[k].set_feature_mode(static_cast<warps::FeatureMode>(c.mode));
    modulator_[k].mutable_parameters()->carrier_shape = c.button;
    modulator_[k].set_bypass(test_transparent_);
    cv_[k].Settle(pots);
    if (has_note_) cv_[k].SettleNoteCv(static_cast<float>(midi_note_ - 60));
  }
}

void Engine::Snap() {
  Config c = Wanted();
  if (!SameConfig(c, applied_)) ApplyConfig(c);
  Pots pots = PotsFor(applied_.mode);
  const ParamDef& dist = kParams[P_DL_DIST];
  const ParamDef& speed = kParams[P_DL_SPEED];
  for (int k = 0; k < 2; ++k) {
    cv_[k].Settle(pots);
    modulator_[k].PrimeDelay((params_[P_DL_DIST] - dist.min) / (dist.max - dist.min),
                             (params_[P_DL_SPEED] - speed.min) / (speed.max - speed.min));
  }
  fade_ = 1.0f;
  fading_out_ = false;
  mix_ = params_[P_MIX];
  out_gain_ = DbToGain(params_[P_OUT_LEVEL]);
  in_gain_ = DbToGain(params_[P_IN_GAIN]);
  bypass_gain_ = params_[P_BYPASS];
}

void Engine::RunWarpsBlock() {
  Config wanted = Wanted();
  if (!SameConfig(wanted, applied_)) {
    if (fade_ <= 0.0f) {
      ApplyConfig(wanted);
      fading_out_ = false;
    } else {
      fading_out_ = true;
    }
  } else {
    fading_out_ = false;   // changed back before the fade finished
  }
  for (int k = 0; k < 2; ++k) modulator_[k].set_bypass(test_transparent_);

  const Config& c = applied_;
  const bool two = UsesTwoModulators(c);
  const bool osc = OscillatorOn(c);
  const bool internal_loop = c.mode == warps::FEATURE_MODE_DELAY && c.button == 0 &&
                             c.loop_patch == 0;

  warps::ShortFrame in[2][kWarpsBlock];
  warps::ShortFrame out[2][kWarpsBlock];
  for (int i = 0; i < kWarpsBlock; ++i) {
    float l = in_l_[i], r = in_r_[i];
    float m = 0.5f * (l + r);
    float in1, in2;
    if (two) {
      // One Warps per channel. With the oscillator on, input 1 is its phase
      // modulation input, so it stays unpatched; the channel is the modulator.
      in[0][i].l = osc ? 0 : ToShort(l);
      in[0][i].r = ToShort(l);
      in[1][i].l = osc ? 0 : ToShort(r);
      in[1][i].r = ToShort(r);
      continue;
    }
    switch (c.routing) {
      case ROUTE_MONO: in1 = m; in2 = m; break;
      case ROUTE_CARRIER: in1 = 0.0f; in2 = m; break;
      default: in1 = l; in2 = r; break;   // DUAL, and PRESERVE for Doppler/delay
    }
    in[0][i].l = ToShort(in1);
    in[0][i].r = internal_loop ? loop_aux_[i] : ToShort(in2);
  }

  const bool midi = params_[P_OSC_SRC] > 0.5f && has_note_;
  const float semis = static_cast<float>(midi_note_ - 60);
  Pots pots = PotsFor(c.mode);
  for (int k = 0; k < (two ? 2 : 1); ++k) {
    cv_[k].Read(pots, midi, semis, modulator_[k].mutable_parameters());
    modulator_[k].Process(in[k], out[k], kWarpsBlock);
  }
  for (int i = 0; i < kWarpsBlock; ++i) loop_aux_[i] = out[0][i].r;

  if (out_read_ > 0) {   // compact the output FIFO
    int n = out_count_ - out_read_;
    memmove(out_l_, out_l_ + out_read_, n * sizeof(float));
    memmove(out_r_, out_r_ + out_read_, n * sizeof(float));
    out_count_ = n;
    out_read_ = 0;
  }
  if (out_count_ + kWarpsBlock > kFifoFrames) {   // cannot happen; never overrun
    ++underruns_;
    return;
  }
  const bool main_only = c.outputs == 1;
  for (int i = 0; i < kWarpsBlock; ++i) {
    float ol, orr;
    if (two) {
      ol = out[0][i].l;
      orr = out[1][i].l;
    } else {
      ol = out[0][i].l;
      orr = main_only ? out[0][i].l : out[0][i].r;
    }
    if (fading_out_) {
      fade_ -= kFadeStep;
      if (fade_ < 0.0f) fade_ = 0.0f;
    } else if (fade_ < 1.0f) {
      fade_ += kFadeStep;
      if (fade_ > 1.0f) fade_ = 1.0f;
    }
    out_l_[out_count_] = ol * (fade_ / 32768.0f);
    out_r_[out_count_] = orr * (fade_ / 32768.0f);
    ++out_count_;
  }
}

void Engine::Process(int16_t* audio, int frames) {
  const float mix_target = params_[P_MIX];
  const float out_target = DbToGain(params_[P_OUT_LEVEL]);
  const float in_target = DbToGain(params_[P_IN_GAIN]);
  const float bypass_target = params_[P_BYPASS];
  for (int f = 0; f < frames; ++f) {
    Smooth(&in_gain_, in_target);
    float xl = audio[2 * f] * (1.0f / 32768.0f);
    float xr = audio[2 * f + 1] * (1.0f / 32768.0f);

    dry_l_[dry_write_] = xl;
    dry_r_[dry_write_] = xr;
    int rd = dry_write_ - dry_delay_;
    if (rd < 0) rd += kMaxDryDelay;
    float dl = dry_l_[rd], dr = dry_r_[rd];
    if (++dry_write_ == kMaxDryDelay) dry_write_ = 0;

    up_.Push(xl * in_gain_, xr * in_gain_);
    while (up_.Ready()) {
      up_.Next(&in_l_[in_count_], &in_r_[in_count_]);
      if (++in_count_ == kWarpsBlock) {
        RunWarpsBlock();
        in_count_ = 0;
      }
    }

    while (!down_.Ready()) {
      if (out_read_ < out_count_) {
        down_.Push(out_l_[out_read_], out_r_[out_read_]);
        ++out_read_;
      } else {
        ++underruns_;
        down_.Push(0.0f, 0.0f);
      }
    }
    float wl, wr;
    down_.Next(&wl, &wr);
    if (!(fabsf(wl) < 1e4f) || !(fabsf(wr) < 1e4f)) {   // NaN / Inf / runaway
      ++nan_resets_;
      Reset96k();
      wl = wr = 0.0f;
    }

    if (params_[P_DC_BLOCK] < 0.5f) {
      float hl = wl - dc_x_[0] + kDcPole * dc_y_[0];
      float hr = wr - dc_x_[1] + kDcPole * dc_y_[1];
      dc_x_[0] = wl;
      dc_x_[1] = wr;
      dc_y_[0] = wl = hl;
      dc_y_[1] = wr = hr;
    }

    Smooth(&out_gain_, out_target);
    Smooth(&mix_, mix_target);
    Smooth(&bypass_gain_, bypass_target);
    float yl = dl + (wl * out_gain_ - dl) * mix_;
    float yr = dr + (wr * out_gain_ - dr) * mix_;
    yl += (dl - yl) * bypass_gain_;
    yr += (dr - yr) * bypass_gain_;
    audio[2 * f] = ToShort(yl);
    audio[2 * f + 1] = ToShort(yr);
  }
}

}  // namespace owarps

// oWarps Parasites - the Warps Parasites DSP as a Schwung audio FX.
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
//
//   44.1 kHz in -> input gain -> 96 kHz (polyphase) -> routing onto Warps'
//   inputs 1 and 2 -> warps::Modulator (Parasites, unmodified algorithms) in
//   the firmware's 60-frame blocks -> outputs (MAIN, AUX) -> fades -> 44.1
//   kHz -> OUTPUT level -> MIX against the dry input delayed by exactly the
//   round trip (scripts/gen_tables.py) -> out.
//
// Nothing here allocates: every buffer is a member, and an Engine lives in
// plugin.cpp's static pool.
#ifndef OWARPS_ENGINE_H_
#define OWARPS_ENGINE_H_

#include <stdint.h>

#include "owarps_params.h"
#include "warps/dsp/modulator.h"
#include "wrapper/cv_reader.h"
#include "wrapper/resampler.h"

namespace owarps {

const int kWarpsBlock = 60;          // FillBuffer's block size (codec.Start(60, ...))
const int kFifoFrames = 512;         // 96 kHz frames; a 128-frame host block yields <= 279
const int kMaxDryDelay = 256;        // >= kRoundTripSamples

enum Routing { ROUTE_DUAL, ROUTE_MONO, ROUTE_CARRIER, ROUTE_PRESERVE };

class Engine {
 public:
  Engine() { }

  // Host rate must be 44100 (the resampler is designed for it).
  bool Init(int host_rate);

  void Process(int16_t* audio, int frames);

  // index: ParamIndex. Values are clamped (enums and ints rounded).
  void SetParam(int index, float value);
  float GetParam(int index) const { return params_[index]; }

  // Skip every glide: the next block starts at the current parameters (used
  // after a state restore, so a preset does not sweep in).
  void Snap();

  void NoteOn(int note) { midi_note_ = note; has_note_ = true; }

  int nan_resets() const { return nan_resets_; }
  int underruns() const { return underruns_; }
  int applied_mode() const { return applied_.mode; }
  int latency_samples() const;

  // Test hook: put the firmware's own bypass (input copied to output) in
  // place of the selected mode, to measure the wrapper alone.
  void set_test_transparent(bool on) { test_transparent_ = on; }

 private:
  // What changes only behind a fade (switching it under audio would click).
  struct Config {
    int mode;
    int routing;
    int outputs;
    int button;      // carrier_shape of the applied mode
    int loop_patch;
  };

  Config Wanted() const;
  bool SameConfig(const Config& a, const Config& b) const;
  bool UsesTwoModulators(const Config& c) const;
  bool OscillatorOn(const Config& c) const;
  Pots PotsFor(int mode) const;
  void ApplyConfig(const Config& c);
  void RunWarpsBlock();
  void Reset96k();

  warps::Modulator modulator_[2];
  CvReader cv_[2];
  Resampler up_;
  Resampler down_;

  float params_[P_COUNT];
  Config applied_;

  // 96 kHz FIFOs: upsampled input waiting for a full Warps block, and Warps
  // output waiting for the downsampler (primed with kSrcPrimeFrames zeros).
  float in_l_[kFifoFrames], in_r_[kFifoFrames];
  int in_count_;
  float out_l_[kFifoFrames], out_r_[kFifoFrames];
  int out_read_, out_count_;

  // The previous block's AUX output, for the delay's open feedback loop
  // patched internally (AUX -> IN 2).
  short loop_aux_[kWarpsBlock];

  // Fade of the wet signal around a Config change: 0 = silent.
  float fade_;
  bool fading_out_;

  // Dry path: the input, delayed by the round trip.
  float dry_l_[kMaxDryDelay], dry_r_[kMaxDryDelay];
  int dry_write_;
  int dry_delay_;

  float mix_, out_gain_, in_gain_;   // smoothed
  float bypass_gain_;          // 1 = bypassed (smoothed)

  // The module's AC-coupled outputs: a 2 Hz one-pole high-pass on the wet
  // signal (DC BLOCK, default on).
  float dc_x_[2], dc_y_[2];

  int midi_note_;
  bool has_note_;
  bool test_transparent_;
  int nan_resets_;
  int underruns_;
};

}  // namespace owarps

#endif  // OWARPS_ENGINE_H_

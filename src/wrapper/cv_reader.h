// oWarps Parasites - the module's pots, read the way the firmware reads them.
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
//
// A transcription of CvScaler::Read() (parasites warps/cv_scaler.cc) for the
// state of a Warps with NOTHING patched into its four CV inputs:
//
//   * every CV reads its calibration offset, so each `cv` term is 0;
//   * the LEVEL 1/2 CV jacks are normalised (the firmware's noise-probe
//     detector says so), which replaces channel_drive[i] with pot^2 and
//     raw_level[i] with pot, and makes the oscillator note note_pot_ + 24.
//
// Order of operations, one-pole smoothing (applied once per 60-frame block,
// i.e. 1600 times a second) and the pot curve are the firmware's, statement
// for statement; tests/firmware_compare.cpp runs the ORIGINAL cv_scaler.cc
// against this with the same pot readings and requires identical output.
//
// The one addition is the oscillator pitch: on the module the LEVEL 1 pot is
// both input 1's level and (with the internal oscillator on) its pitch. Here
// they are two parameters, so `note_pot` is the pot position the pitch
// parameter stands for; it goes through the firmware's own smoothing. With
// MIDI pitch, the played note arrives the way the firmware treats a V/Oct CV
// on LEVEL 1 (note_cv_: jumps of more than 0.4 semitone are taken at once,
// smaller ones glide), added to the knob's note relative to middle C.
#ifndef OWARPS_CV_READER_H_
#define OWARPS_CV_READER_H_

#include "stmlib/dsp/dsp.h"
#include "warps/dsp/parameters.h"
#include "warps/resources.h"

namespace owarps {

struct Pots {
  float level_1;    // ADC_LEVEL_1_POT
  float level_2;    // ADC_LEVEL_2_POT
  float algorithm;  // ADC_ALGORITHM_POT (the big knob)
  float parameter;  // ADC_PARAMETER_POT (TIMBRE)
  float note_pot;   // what LEVEL 1 reads for the oscillator pitch
};

class CvReader {
 public:
  void Init() {
    for (int i = 0; i < 4; ++i) lp_pot_[i] = 0.0f;
    note_pot_ = 0.0f;
    note_cv_ = 0.0f;
  }

  // Starts the smoothing at the given pots, as though they had not moved
  // since power-on (no glide from zero when a preset loads).
  void Settle(const Pots& pots) {
    lp_pot_[0] = pots.level_1;
    lp_pot_[1] = pots.level_2;
    lp_pot_[2] = pots.algorithm;
    lp_pot_[3] = pots.parameter;
    note_pot_ = 60.0f * pots.note_pot + 12.0f;
  }

  void SettleNoteCv(float semitones) { note_cv_ = semitones; }

  // midi: true when a played note drives the pitch; then `midi_semitones` is
  // that note minus 60.
  void Read(const Pots& pots, bool midi, float midi_semitones, warps::Parameters* p) {
    using stmlib::Interpolate;
    // BIND(p->channel_drive[0], LEVEL_1, false, 1.6f, 0.5f, true) and LEVEL_2:
    // the pot is smoothed; the value (pot * pot * cv * 1.6) is 0 with cv = 0.
    lp_pot_[0] += 0.33f * 0.5f * (pots.level_1 - lp_pot_[0]);
    p->channel_drive[0] = 0.0f;
    lp_pot_[1] += 0.33f * 0.5f * (pots.level_2 - lp_pot_[1]);
    p->channel_drive[1] = 0.0f;
    // BIND(p->modulation_algorithm, ALGORITHM, true, 2.0f, 0.08f, false)
    lp_pot_[2] += 0.33f * 0.08f * (pots.algorithm - lp_pot_[2]);
    {
      float value = UnwrapPot(lp_pot_[2]) + 0.0f * 2.0f;
      CONSTRAIN(value, 0.0f, 1.0f);
      p->modulation_algorithm = value;
    }
    // BIND(p->modulation_parameter, PARAMETER, false, 2.0f, 0.08f, false)
    lp_pot_[3] += 0.33f * 0.08f * (pots.parameter - lp_pot_[3]);
    {
      float value = lp_pot_[3] + 0.0f * 2.0f;
      CONSTRAIN(value, 0.0f, 1.0f);
      p->modulation_parameter = value;
    }

    // Prevent wavefolder bleed caused by a slight offset in the pot or ADC.
    if (p->modulation_algorithm <= 0.125f) {
      p->modulation_algorithm = p->modulation_algorithm * 1.08f - 0.01f;
      CONSTRAIN(p->modulation_algorithm, 0.0f, 1.0f);
    }

    // Raw parameter mappings (no scaling).
    p->raw_algorithm_pot = UnwrapPot(lp_pot_[2]);
    float raw_algorithm_cv = 0.0f;
    p->raw_algorithm_cv = raw_algorithm_cv * 2.0f;
    CONSTRAIN(p->raw_algorithm_cv, -1.0f, 1.0f);
    float raw_algorithm = lp_pot_[2] + raw_algorithm_cv * 2.0f;
    CONSTRAIN(raw_algorithm, 0.0f, 1.0f);
    p->raw_algorithm = raw_algorithm;

    // raw_level[i] (pot * cv * 1.6 = 0) is replaced below by normalisation.

    // Internal oscillator parameters.
    float note = 60.0f * pots.note_pot + 12.0f;
    note_pot_ += 0.1f * (note - note_pot_);

    if (midi) {
      // The firmware's note_cv_ tracking of a V/Oct CV on LEVEL 1.
      float interval = midi_semitones - note_cv_;
      if (interval < -0.4f || interval > 0.4f) {
        note_cv_ = midi_semitones;
      } else {
        note_cv_ += 0.1f * interval;
      }
    }

    // Both LEVEL CV inputs normalised (nothing patched).
    for (int i = 0; i < 2; ++i) {
      float pot = lp_pot_[i];
      p->channel_drive[i] = pot * pot;
      p->raw_level[i] = pot;
    }
    p->note = note_pot_ + 24.0f + (midi ? note_cv_ : 0.0f);
  }

  static float UnwrapPot(float x) {
    return stmlib::Interpolate(warps::lut_pot_curve, x, 512.0f);
  }

 private:
  float lp_pot_[4];  // level 1, level 2, algorithm, parameter
  float note_pot_;
  float note_cv_;
};

}  // namespace owarps

#endif  // OWARPS_CV_READER_H_

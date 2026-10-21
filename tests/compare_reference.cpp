// oWarps Parasites - the REFERENCE half of tests/firmware_compare: the
// original Parasites code, compiled from build/pristine (scripts/fetch_warps.sh
// --pristine), driven the way warps.cc's FillBuffer drives it:
//
//   cv_scaler.Read(modulator.mutable_parameters());
//   modulator.Process(input, output, 60);
//
// with the original CvScaler (cv_scaler.cc) reading stand-in ADC values
// (tests/firmware_stubs). Built with -Dwarps=warps_ref -Dstmlib=stmlib_ref so
// it links beside the port.
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "../tests/compare_common.h"

#define private public   // to start CvScaler from its settled state
#include "warps/cv_scaler.h"
#undef private
#include "warps/dsp/modulator.h"
#include "warps/settings.h"

namespace warps {
TestAdcState test_adc;
}

using namespace warps;

static Modulator modulator;    // static storage, as on the module
static CvScaler cv_scaler;
static CalibrationData calibration;

extern "C" void run_reference(const Scenario* s, short* out) {
  // settings.cc's factory calibration.
  calibration.pitch_offset = 100.00f;
  calibration.pitch_scale = -110.00f;
  calibration.offset[0] = 0.909f;
  calibration.offset[1] = 0.909f;
  calibration.offset[2] = 0.495f;
  calibration.offset[3] = 0.505f;
  calibration.normalization_detection_threshold[0] = 0.85f;
  calibration.normalization_detection_threshold[1] = 0.85f;

  modulator.Init(96000.0f);
  modulator.set_feature_mode(static_cast<FeatureMode>(s->mode));
  modulator.mutable_parameters()->carrier_shape = s->button;
  cv_scaler.Init(&calibration);

  // Settled: the unpatched ALGORITHM/PARAMETER CVs read exactly their
  // calibration offsets (so their `cv` terms are 0), the pots have not moved
  // since power-on, and both LEVEL jacks are detected as unpatched.
  float pots[4];
  PotsAt(*s, 0, pots);
  test_adc.values[ADC_ALGORITHM_CV] = calibration.offset[2];
  test_adc.values[ADC_PARAMETER_CV] = calibration.offset[3];
  cv_scaler.lp_state_[ADC_ALGORITHM_CV] = calibration.offset[2];
  cv_scaler.lp_state_[ADC_PARAMETER_CV] = calibration.offset[3];
  cv_scaler.lp_state_[ADC_LEVEL_1_POT] = pots[0];
  cv_scaler.lp_state_[ADC_LEVEL_2_POT] = pots[1];
  cv_scaler.lp_state_[ADC_ALGORITHM_POT] = pots[2];
  cv_scaler.lp_state_[ADC_PARAMETER_POT] = pots[3];
  cv_scaler.note_pot_ = 60.0f * pots[0] + 12.0f;
  for (int i = 0; i < 2; ++i) {
    cv_scaler.normalization_detector_[i].score_ = 1.0f;
    cv_scaler.normalization_detector_[i].state_ = true;
  }
  test_adc.probe_now = test_adc.probe_prev = false;

  ShortFrame in[kCmpBlock], o[kCmpBlock];
  long n = 0;
  for (int b = 0; b < s->blocks; ++b) {
    PotsAt(*s, b, pots);
    test_adc.values[ADC_LEVEL_1_POT] = pots[0];
    test_adc.values[ADC_LEVEL_2_POT] = pots[1];
    test_adc.values[ADC_ALGORITHM_POT] = pots[2];
    test_adc.values[ADC_PARAMETER_POT] = pots[3];
    for (int i = 0; i < kCmpBlock; ++i, ++n) {
      in[i].l = TestSignal(s->sig1, n);
      in[i].r = TestSignal(s->sig2, n);
    }
    cv_scaler.Read(modulator.mutable_parameters());
    modulator.Process(in, o, kCmpBlock);
    for (int i = 0; i < kCmpBlock; ++i) {
      *out++ = o[i].l;
      *out++ = o[i].r;
    }
  }
}

// Asked by the driver: is CvScaler still reading both LEVEL jacks as
// unpatched at the end of a run (i.e. did the stand-in ADC keep the
// firmware's normalisation detector convinced)?
extern "C" int reference_levels_normalized() {
  return cv_scaler.normalization_detector_[0].normalized() &&
         cv_scaler.normalization_detector_[1].normalized();
}

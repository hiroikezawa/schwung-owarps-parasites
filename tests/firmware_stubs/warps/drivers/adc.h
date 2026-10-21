// Test stand-in for the firmware's warps/drivers/adc.h: the eight ADC
// channels are floats the test sets. Convert() (called at the end of
// CvScaler::Read) latches what the LEVEL CV jacks read from the normalisation
// probe as it was one Read earlier - the two-Read lag CvScaler::
// DetectNormalization correlates against (it uses normalization_probe_value_[1]).
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#ifndef WARPS_DRIVERS_ADC_H_
#define WARPS_DRIVERS_ADC_H_
#include "stmlib/stmlib.h"
namespace warps {
enum AdcChannel {
  ADC_LEVEL_1_CV, ADC_LEVEL_2_CV, ADC_ALGORITHM_CV, ADC_PARAMETER_CV,
  ADC_LEVEL_1_POT, ADC_LEVEL_2_POT, ADC_ALGORITHM_POT, ADC_PARAMETER_POT,
  ADC_LAST
};
struct TestAdcState {
  float values[ADC_LAST];
  bool probe_now;    // last value written to the probe
  bool probe_prev;   // the one before
};
extern TestAdcState test_adc;
class Adc {
 public:
  Adc() { }
  ~Adc() { }
  void Init() { }
  void Convert() {
    // Unpatched LEVEL CV jacks follow the probe (settings.cc: 0.79 HIGH, 0.91 LOW).
    float v = test_adc.probe_prev ? 0.79f : 0.91f;
    test_adc.values[ADC_LEVEL_1_CV] = v;
    test_adc.values[ADC_LEVEL_2_CV] = v;
    test_adc.probe_prev = test_adc.probe_now;
  }
  inline float float_value(uint8_t channel) const { return test_adc.values[channel]; }
  inline uint16_t value(uint8_t channel) const {
    return static_cast<uint16_t>(test_adc.values[channel] * 65535.0f);
  }
};
}  // namespace warps
#endif

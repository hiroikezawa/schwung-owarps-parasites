// Test stand-in for warps/drivers/normalization_probe.h (a GPIO on the module).
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#ifndef WARPS_DRIVERS_NORMALIZATION_PROBE_H_
#define WARPS_DRIVERS_NORMALIZATION_PROBE_H_
#include "warps/drivers/adc.h"
namespace warps {
class NormalizationProbe {
 public:
  void Init() { }
  static inline void High() { test_adc.probe_now = true; }
  static inline void Low() { test_adc.probe_now = false; }
  static inline void Write(bool value) { test_adc.probe_now = value; }
};
}  // namespace warps
#endif

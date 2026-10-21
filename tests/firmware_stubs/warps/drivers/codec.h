// Test stand-in for warps/drivers/codec.h: only the frame type CvScaler uses.
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#ifndef WARPS_DRIVERS_CODEC_H_
#define WARPS_DRIVERS_CODEC_H_
#include "stmlib/stmlib.h"
namespace warps {
class Codec {
 public:
  typedef struct { short l; short r; } Frame;
};
}  // namespace warps
#endif

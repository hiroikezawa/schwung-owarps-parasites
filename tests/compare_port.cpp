// oWarps Parasites - the PORT half of tests/firmware_compare: the vendored
// (patched) Modulator and the transcribed pot reading of src/wrapper/
// cv_reader.h, driven exactly as compare_reference.cpp drives the original.
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#include <cstring>

#include "../tests/compare_common.h"
#include "warps/dsp/modulator.h"
#include "stmlib/utils/random.h"
#include "wrapper/cv_reader.h"

using namespace warps;

static Modulator modulator;
static owarps::CvReader cv;

extern "C" void run_port(const Scenario* s, short* out) {
  modulator.Init(96000.0f);
  modulator.set_feature_mode(static_cast<FeatureMode>(s->mode));
  modulator.mutable_parameters()->carrier_shape = s->button;
  cv.Init();

  float pots[4];
  owarps::Pots p;
  PotsAt(*s, 0, pots);
  p.level_1 = pots[0];
  p.level_2 = pots[1];
  p.algorithm = pots[2];
  p.parameter = pots[3];
  p.note_pot = pots[0];   // one pot on the module
  cv.Settle(p);

  ShortFrame in[kCmpBlock], o[kCmpBlock];
  long n = 0;
  for (int b = 0; b < s->blocks; ++b) {
    PotsAt(*s, b, pots);
    p.level_1 = pots[0];
    p.level_2 = pots[1];
    p.algorithm = pots[2];
    p.parameter = pots[3];
    p.note_pot = pots[0];
    for (int i = 0; i < kCmpBlock; ++i, ++n) {
      in[i].l = TestSignal(s->sig1, n);
      in[i].r = TestSignal(s->sig2, n);
    }
    cv.Read(p, false, 0.0f, modulator.mutable_parameters());
    // The firmware's CvScaler::DetectNormalization draws one random word per
    // block for its probe; the analog delay's hiss shares that generator.
    stmlib::Random::GetWord();
    modulator.Process(in, o, kCmpBlock);
    for (int i = 0; i < kCmpBlock; ++i) {
      *out++ = o[i].l;
      *out++ = o[i].r;
    }
  }
}

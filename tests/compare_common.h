// oWarps Parasites - shared by the two halves of tests/firmware_compare.
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#ifndef OWARPS_COMPARE_COMMON_H_
#define OWARPS_COMPARE_COMMON_H_

#include <math.h>
#include <stdint.h>

const int kCmpBlock = 60;
const int kCmpMaxBlocks = 1200;

enum Signal { SIG_SINE, SIG_SAW, SIG_SQUARE, SIG_NOISE, SIG_IMPULSE, SIG_SILENCE, SIG_DRUMS,
              SIG_COUNT };

struct Scenario {
  int mode;           // warps::FeatureMode
  int button;         // carrier_shape 0..3
  float pots0[4];     // level 1, level 2, algorithm, parameter at the first block
  float pots1[4];     // ... at the last block (linear in between)
  int sig1, sig2;     // inputs 1 and 2
  int blocks;
};

// Test signals at 96 kHz, as the codec delivers them (int16).
static inline short TestSignal(int sig, long n) {
  const double t = n / 96000.0;
  double x = 0.0;
  switch (sig) {
    case SIG_SINE: x = 0.5 * sin(2.0 * M_PI * 220.0 * t); break;
    case SIG_SAW: x = 0.5 * (2.0 * fmod(330.0 * t, 1.0) - 1.0); break;
    case SIG_SQUARE: x = fmod(110.0 * t, 1.0) < 0.5 ? 0.4 : -0.4; break;
    case SIG_NOISE: {
      uint32_t s = static_cast<uint32_t>(n) * 1664525u + 1013904223u;
      s ^= s >> 15; s *= 2246822519u; s ^= s >> 13;
      x = 0.4 * ((s & 0xffff) / 32768.0 - 1.0);
      break;
    }
    case SIG_IMPULSE: x = (n % 4800) == 0 ? 0.9 : 0.0; break;
    case SIG_SILENCE: x = 0.0; break;
    case SIG_DRUMS: {   // 120 BPM: kick on beats, snare on 2 and 4, closed hat on 8ths
      double beat = fmod(t, 0.5), eighth = fmod(t, 0.25);
      int bar_beat = static_cast<int>(fmod(t, 2.0) / 0.5);
      x += 0.7 * sin(2.0 * M_PI * (50.0 * beat + 60.0 * (1.0 - exp(-beat * 30.0)) / 30.0))
           * exp(-beat * 9.0);
      uint32_t s = static_cast<uint32_t>(n) * 22695477u + 1u;
      s ^= s >> 16; s *= 0x45d9f3bu; s ^= s >> 16;
      double noise = (s & 0xffff) / 32768.0 - 1.0;
      if (bar_beat == 1 || bar_beat == 3) x += 0.35 * noise * exp(-beat * 25.0);
      x += 0.12 * noise * exp(-eighth * 120.0);
      break;
    }
  }
  if (x > 0.999) x = 0.999;
  if (x < -0.999) x = -0.999;
  return static_cast<short>(lrint(x * 32767.0));
}

static inline void PotsAt(const Scenario& s, int block, float pots[4]) {
  float f = s.blocks > 1 ? static_cast<float>(block) / (s.blocks - 1) : 0.0f;
  for (int i = 0; i < 4; ++i) pots[i] = s.pots0[i] + (s.pots1[i] - s.pots0[i]) * f;
}

// Each half writes 2 * kCmpBlock * blocks shorts (L, R interleaved).
extern "C" void run_reference(const Scenario* s, short* out);
extern "C" void run_port(const Scenario* s, short* out);

#endif  // OWARPS_COMPARE_COMMON_H_

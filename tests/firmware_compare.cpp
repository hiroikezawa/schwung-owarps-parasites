// oWarps Parasites - does the port compute what the Parasites firmware does?
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
//
// Links two copies of Warps: the ORIGINAL Parasites sources (build/reference:
// the pinned upstream files, unpatched except for the Cortex-M4 shift noted
// below) with the original CvScaler, and the PORT (src/mi + cv_reader.h).
// Every scenario - 9 modes x 4 button states x 5 knob settings (static,
// extremes, and a sweep of every knob across its whole range) x 5 input pairs
// (sine, saw, square, white noise, impulses, silence, a drum loop) - runs in
// a fresh forked process (the original keeps state in function-local
// statics) and the two outputs are compared sample for sample.
//
// The reference's bit-mangler line `x_1_mod << (x_2_mod >> 12)` shifts by a
// negative count, which each CPU answers differently; tests/test.sh rewrites
// that one line in build/reference to the Cortex-M4 answer (the module's) so
// the comparison is against the module rather than against this machine.
//
//   firmware_compare [--quick] [--stats]
// Exit 0 only if every scenario matches bit for bit.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../tests/compare_common.h"

extern "C" int reference_levels_normalized();

namespace {

const char* const kModeNames[] = {"DOPPLER", "WAVEFOLDER", "CHEBYSCHEV", "FREQ SHIFT",
                                  "BIT MANGLER", "COMPARATOR", "VOCODER", "DELAY", "META"};
const char* const kSigNames[] = {"sine", "saw", "square", "noise", "impulse", "silence", "drums"};

struct Result {
  long samples;
  long mismatches;
  int max_diff;
  int normalized;
  double rms_ref, rms_port;
  int peak_ref, peak_port;
};

short g_ref[2 * kCmpBlock * kCmpMaxBlocks];
short g_port[2 * kCmpBlock * kCmpMaxBlocks];

Result RunOne(const Scenario& s) {
  Result r;
  memset(&r, 0, sizeof(r));
  run_reference(&s, g_ref);
  r.normalized = reference_levels_normalized();
  run_port(&s, g_port);
  r.samples = 2L * kCmpBlock * s.blocks;
  double sr = 0, sp = 0;
  for (long i = 0; i < r.samples; ++i) {
    int d = abs(g_ref[i] - g_port[i]);
    if (d) ++r.mismatches;
    if (d > r.max_diff) r.max_diff = d;
    sr += static_cast<double>(g_ref[i]) * g_ref[i];
    sp += static_cast<double>(g_port[i]) * g_port[i];
    if (abs(g_ref[i]) > r.peak_ref) r.peak_ref = abs(g_ref[i]);
    if (abs(g_port[i]) > r.peak_port) r.peak_port = abs(g_port[i]);
  }
  if (getenv("CMP_DEBUG") && r.mismatches) {
    int shown = 0;
    for (long i = 0; i < r.samples && shown < 6; ++i) {
      if (g_ref[i] != g_port[i]) {
        fprintf(stderr, "      [%ld block %ld %c] ref %d port %d\n", i / 2, i / 2 / kCmpBlock,
                (i & 1) ? 'R' : 'L', g_ref[i], g_port[i]);
        ++shown;
      }
    }
  }
  r.rms_ref = sqrt(sr / r.samples) / 32768.0;
  r.rms_port = sqrt(sp / r.samples) / 32768.0;
  return r;
}

Result RunForked(const Scenario& s) {
  int fd[2];
  if (pipe(fd)) { perror("pipe"); exit(2); }
  pid_t pid = fork();
  if (pid == 0) {
    close(fd[0]);
    Result r = RunOne(s);
    if (write(fd[1], &r, sizeof(r)) != sizeof(r)) _exit(3);
    _exit(0);
  }
  close(fd[1]);
  Result r;
  memset(&r, 0, sizeof(r));
  if (read(fd[0], &r, sizeof(r)) != sizeof(r)) r.mismatches = -1;
  close(fd[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status)) r.mismatches = -1;
  return r;
}

double Db(double rms) { return rms > 0 ? 20.0 * log10(rms) : -999.0; }

}  // namespace

int main(int argc, char** argv) {
  bool quick = argc > 1 && !strcmp(argv[1], "--quick");
  bool stats = argc > 1 && !strcmp(argv[argc - 1], "--stats");
  const float pot_sets[5][2][4] = {
    {{0.7f, 0.7f, 0.5f, 0.5f}, {0.7f, 0.7f, 0.5f, 0.5f}},
    {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {{1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{0.4f, 0.95f, 0.15f, 0.85f}, {0.4f, 0.95f, 0.15f, 0.85f}},
    {{0.2f, 0.9f, 0.0f, 1.0f}, {0.9f, 0.3f, 1.0f, 0.0f}},   // every knob swept
  };
  const int sig_pairs[5][2] = {{SIG_SINE, SIG_SAW}, {SIG_SQUARE, SIG_NOISE},
                               {SIG_IMPULSE, SIG_SILENCE}, {SIG_DRUMS, SIG_SINE},
                               {SIG_SILENCE, SIG_SILENCE}};
  long total = 0, bad_scen = 0, scen = 0, not_norm = 0;
  for (int mode = 0; mode < 9; ++mode) {
    long mode_mis = 0;
    int mode_scen = 0, mode_max = 0;
    double mode_rms = 0;
    int mode_peak = 0;
    for (int button = 0; button < 4; ++button) {
      for (int ps = 0; ps < 5; ++ps) {
        for (int sp = 0; sp < 5; ++sp) {
          if (quick && (ps % 2 || sp % 2)) continue;
          Scenario s;
          s.mode = mode;
          s.button = button;
          memcpy(s.pots0, pot_sets[ps][0], sizeof(s.pots0));
          memcpy(s.pots1, pot_sets[ps][1], sizeof(s.pots1));
          s.sig1 = sig_pairs[sp][0];
          s.sig2 = sig_pairs[sp][1];
          // The two tape modes run long enough to wrap their 13821-frame line.
          s.blocks = (mode == 0 || mode == 7) ? 1000 : 400;
          Result r = RunForked(s);
          ++scen;
          ++mode_scen;
          total += r.samples;
          if (!r.normalized) ++not_norm;
          if (r.mismatches) {
            ++bad_scen;
            mode_mis += r.mismatches > 0 ? r.mismatches : 0;
            if (r.max_diff > mode_max) mode_max = r.max_diff;
            if (bad_scen <= 100000) {
              printf("  MISMATCH %-11s button %d knobs %d inputs %s/%s: %ld samples differ "
                     "(max %d), rms %.1f/%.1f dB\n", kModeNames[mode], button, ps,
                     kSigNames[s.sig1], kSigNames[s.sig2], r.mismatches, r.max_diff,
                     Db(r.rms_ref), Db(r.rms_port));
            }
          }
          if (r.rms_ref > mode_rms) mode_rms = r.rms_ref;
          if (r.peak_ref > mode_peak) mode_peak = r.peak_ref;
          if (stats) {
            printf("    %-11s b%d k%d %-7s/%-7s ref rms %6.1f dB peak %5d | port rms %6.1f dB "
                   "peak %5d\n", kModeNames[mode], button, ps, kSigNames[s.sig1],
                   kSigNames[s.sig2], Db(r.rms_ref), r.peak_ref, Db(r.rms_port), r.peak_port);
          }
        }
      }
    }
    printf("%-11s %3d scenarios: %s (max rms %.1f dBFS, peak %d)\n", kModeNames[mode],
           mode_scen, mode_mis ? "DIFFERENT" : "bit-identical", Db(mode_rms), mode_peak);
  }
  printf("%ld scenarios, %ld output samples per side; %ld scenarios differ\n", scen, total,
         bad_scen);
  if (not_norm) {
    printf("FAIL: the reference CvScaler lost the unpatched-LEVEL detection in %ld scenarios\n",
           not_norm);
    return 1;
  }
  if (bad_scen) {
    printf("FAIL\n");
    return 1;
  }
  printf("PASS: port == Parasites firmware, bit for bit\n");
  return 0;
}

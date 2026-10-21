/* oWarps Parasites - load test and timing of the REAL owarps_parasites.so on
 * the target (scripts/bench_move.sh runs it on the Move).
 *
 * dlopen()s the module as Schwung's chain host does, through the audio FX v2
 * ABI at Move's 44.1 kHz / 128 frames, checks it creates, processes, takes a
 * state and destroys, then times process_block (the SPI-callback cost) for
 * every mode x button state, in DUAL and PRESERVE routing, with a drum loop
 * running through it; and reports memory (RSS) and create / preset-load time.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host/audio_fx_api_v2.h"

static double Now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static long RssKb(void) {
  FILE *f = fopen("/proc/self/status", "r");
  char line[256];
  long kb = -1;
  while (f && fgets(line, sizeof(line), f)) {
    if (!strncmp(line, "VmRSS:", 6)) kb = (long)strtod(line + 6, NULL);   /* not strtol: glibc 2.38 renames it */
  }
  if (f) fclose(f);
  return kb;
}

/* fmod() is versioned GLIBC_2.38 by new toolchains; the Move's libc is older. */
static double Wrap(double x, double y) { return x - floor(x / y) * y; }

static void Drums(int16_t *io, int frames, long *n) {
  for (int i = 0; i < frames; ++i, ++*n) {
    double t = *n / 44100.0, beat = Wrap(t, 0.5);
    double x = 0.7 * sin(2 * M_PI * (50 * beat + 2.0 * (1 - exp(-beat * 30)))) * exp(-beat * 9);
    unsigned s = (unsigned)*n * 22695477u + 1u;
    s ^= s >> 16;
    s *= 0x45d9f3bu;
    s ^= s >> 16;
    x += 0.15 * ((s & 0xffff) / 32768.0 - 1.0) * exp(-Wrap(t, 0.25) * 60);
    io[2 * i] = io[2 * i + 1] = (int16_t)lrint(x * 22000);
  }
}

static int CompareDouble(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return x < y ? -1 : x > y;
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "./owarps_parasites.so";
  long rss0 = RssKb();
  void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!lib) { printf("FAIL: dlopen: %s\n", dlerror()); return 1; }
  audio_fx_api_v2_t *(*init)(const host_api_v1_t *) =
      (audio_fx_api_v2_t *(*)(const host_api_v1_t *))dlsym(lib, "move_audio_fx_init_v2");
  if (!init) { printf("FAIL: no move_audio_fx_init_v2\n"); return 1; }
  static host_api_v1_t host;
  memset(&host, 0, sizeof(host));
  host.api_version = 1;
  host.sample_rate = 44100;
  host.frames_per_block = 128;
  audio_fx_api_v2_t *api = init(&host);
  if (!api || api->api_version != 2) { printf("FAIL: v2 init\n"); return 1; }
  long rss_lib = RssKb();
  double a = Now();
  void *p = api->create_instance(".", NULL);
  double create_us = Now() - a;
  if (!p) { printf("FAIL: create_instance\n"); return 1; }
  static char buf[131072];
  if (api->get_param(p, "chain_params", buf, sizeof(buf)) <= 0 || !strstr(buf, "\"dl_topo\"")) {
    printf("FAIL: chain_params\n");
    return 1;
  }
  int16_t io[256];
  long n = 0;
  int peak = 0;
  for (int b = 0; b < 400; ++b) {
    Drums(io, 128, &n);
    api->process_block(p, io, 128);
    for (int i = 0; i < 256; ++i) if (abs(io[i]) > peak) peak = abs(io[i]);
  }
  long rss_run = RssKb();
  if (peak < 1000) { printf("FAIL: silent (peak %d)\n", peak); return 1; }
  if (api->get_param(p, "state", buf, sizeof(buf)) <= 0) { printf("FAIL: state\n"); return 1; }
  a = Now();
  api->set_param(p, "state", buf);
  double state_us = Now() - a;
  printf("PASS: dlopen + v2 ABI on this machine: created, processing (peak %d), state %zu bytes\n",
         peak, strlen(buf));
  printf("memory: RSS %ld kB before dlopen, +%ld kB library (static pool, untouched), "
         "+%ld kB with one instance running\n", rss0, rss_lib - rss0, rss_run - rss_lib);

  static const char *const kModes[9] = {"DOPPLER", "WAVEFOLDER", "CHEBYSCHEV", "FREQ SHIFT",
                                        "BIT MANGLER", "COMPARATOR", "VOCODER", "DELAY", "META"};
  static const char *const kButton[9] = {"dop_room", "fold_osc", "cheb_osc", "fs_car", "bit_osc",
                                         "cmp_osc", "voc_osc", "dl_topo", "meta_osc"};
  const double budget = 128 / 44100.0 * 1e6;
  printf("process_block, 128 frames (budget %.0f us), drum loop in, knob 2 sweeping:\n", budget);
  double worst_mean = 0.0;
  const char *worst_name = "";
  static double t[3000];
  for (int mode = 0; mode < 9; ++mode) {
    for (int route = 0; route < 2; ++route) {
      for (int button = 0; button < 4; ++button) {
        char v[16];
        snprintf(v, sizeof(v), "%d", mode);
        api->set_param(p, "mode", v);
        snprintf(v, sizeof(v), "%d", button);
        api->set_param(p, kButton[mode], v);
        api->set_param(p, "routing", route ? "PRESERVE" : "DUAL");
        api->set_param(p, "mix", "1");
        char key[32];
        static const char *const kAlgo[9] = {"dop_x", "fold_amt", "cheb_order", "fs_shift", "bit_degr",
                                             "cmp_func", "voc_warp", "dl_speed", "meta_algo"};
        snprintf(key, sizeof(key), "%s", kAlgo[mode]);
        for (int b = 0; b < 40; ++b) { Drums(io, 128, &n); api->process_block(p, io, 128); }
        const int count = 3000;
        double sum = 0;
        for (int b = 0; b < count; ++b) {
          snprintf(v, sizeof(v), "%.3f", 0.2 + 0.6 * ((b % 1000) / 1000.0));
          api->set_param(p, key, v);
          Drums(io, 128, &n);
          double s = Now();
          api->process_block(p, io, 128);
          t[b] = Now() - s;
          sum += t[b];
        }
        qsort(t, count, sizeof(double), CompareDouble);
        double mean = sum / count;
        printf("  %-11s %-8s button %d  mean %6.1f us (%4.1f %%)  p99 %6.1f  max %7.1f us\n",
               kModes[mode], route ? "PRESERVE" : "DUAL", button, mean, 100 * mean / budget,
               t[count * 99 / 100], t[count - 1]);
        if (mean > worst_mean) {
          worst_mean = mean;
          worst_name = kModes[mode];
        }
      }
    }
  }
  char underruns[32] = "", nans[32] = "";
  api->get_param(p, "underruns", underruns, sizeof(underruns));
  api->get_param(p, "nan_resets", nans, sizeof(nans));
  api->destroy_instance(p);
  printf("create_instance %.0f us, preset load (state) %.0f us; heaviest: %s, mean %.1f us/block "
         "(%.1f %%); underruns %s, NaN resets %s\n", create_us, state_us, worst_name, worst_mean,
         100 * worst_mean / budget, underruns, nans);
  dlclose(lib);
  return 0;
}

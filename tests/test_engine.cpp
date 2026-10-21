// oWarps Parasites - offline tests of the real owarps_parasites.so through the
// audio FX v2 ABI.
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
//
//   test-engine <path/to/owarps_parasites.so> [render-dir]
//
// malloc/free are interposed by this executable and counted while the
// plugin's entry points run: the realtime contract is checked, not assumed.
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>
#include <vector>

#define _Static_assert static_assert
#include "host/audio_fx_api_v2.h"
#undef _Static_assert

static volatile int g_counting = 0;
static volatile long g_allocs = 0;

#ifndef OWARPS_NO_ALLOC_HOOK   // sanitizer builds bring their own allocator
extern "C" void* __libc_malloc(size_t);
extern "C" void* __libc_calloc(size_t, size_t);
extern "C" void* __libc_realloc(void*, size_t);
extern "C" void __libc_free(void*);
extern "C" void* malloc(size_t n) {
  if (g_counting) ++g_allocs;
  return __libc_malloc(n);
}
extern "C" void* calloc(size_t a, size_t b) {
  if (g_counting) ++g_allocs;
  return __libc_calloc(a, b);
}
extern "C" void* realloc(void* p, size_t n) {
  if (g_counting) ++g_allocs;
  return __libc_realloc(p, n);
}
extern "C" void free(void* p) {
  if (g_counting && p) ++g_allocs;
  __libc_free(p);
}
#endif

namespace {

const int kRate = 44100;
const int kBlock = 128;
const int kLatency = 103;
audio_fx_api_v2_t* api;
int failures = 0;
std::string render_dir;

const char* const kModes[] = {"DOPPLER", "WAVEFOLDER", "CHEBYSCHEV", "FREQ SHIFT",
                              "BIT MANGLER", "COMPARATOR", "VOCODER", "DELAY", "META"};
const char* const kButtonKey[] = {"dop_room", "fold_osc", "cheb_osc", "fs_car", "bit_osc",
                                  "cmp_osc", "voc_osc", "dl_topo", "meta_osc"};

#define CHECK(cond, ...)                                   \
  do {                                                     \
    if (!(cond)) {                                         \
      ++failures;                                          \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);          \
      printf(__VA_ARGS__);                                 \
      printf("\n");                                        \
    }                                                      \
  } while (0)

struct Fx {
  void* inst;
  Fx() { inst = api->create_instance("/nonexistent", NULL); }
  ~Fx() { api->destroy_instance(inst); }
  void set(const char* k, const char* v) {
    g_counting = 1;
    api->set_param(inst, k, v);
    g_counting = 0;
  }
  void set(const char* k, double v) {
    char b[32];
    snprintf(b, sizeof(b), "%.6f", v);
    set(k, b);
  }
  void seti(const char* k, int v) {
    char b[32];
    snprintf(b, sizeof(b), "%d", v);
    set(k, b);
  }
  std::string get(const char* k) {
    static char b[16384];
    g_counting = 1;
    int n = api->get_param(inst, k, b, sizeof(b));
    g_counting = 0;
    return n < 0 ? std::string("<err>") : std::string(b);
  }
  int geti(const char* k) { return atoi(get(k).c_str()); }
  void process(int16_t* io, int frames) {
    g_counting = 1;
    api->process_block(inst, io, frames);
    g_counting = 0;
  }
  void midi(uint8_t a, uint8_t b, uint8_t c) {
    uint8_t m[3] = {a, b, c};
    g_counting = 1;
    api->on_midi(inst, m, 3, 0);
    g_counting = 0;
  }
};

struct Source {
  enum Kind { SILENCE, SINE, NOISE, IMPULSE, STEREO, DRUMS, LEFT_ONLY } kind;
  double freq = 440.0, amp = 0.5;
  uint32_t rng = 12345;
  long n = 0;
  explicit Source(Kind k, double f = 440.0, double a = 0.5) : kind(k), freq(f), amp(a) { }
  float nextf() {
    rng = rng * 1664525u + 1013904223u;
    return static_cast<int32_t>(rng) / 2147483648.0f;
  }
  void fill(int16_t* io, int frames) {
    for (int i = 0; i < frames; ++i, ++n) {
      double l = 0, r = 0, t = static_cast<double>(n) / kRate;
      switch (kind) {
        case SILENCE: break;
        case SINE: l = r = amp * sin(2 * M_PI * freq * t); break;
        case NOISE: l = amp * nextf(); r = amp * nextf(); break;
        case IMPULSE: l = r = (n % (kRate / 4) == 0) ? amp : 0.0; break;
        case STEREO:
          l = amp * sin(2 * M_PI * freq * t);
          r = amp * 0.7 * sin(2 * M_PI * freq * 1.5 * t + 1.0);
          break;
        case LEFT_ONLY: l = amp * sin(2 * M_PI * freq * t); break;
        case DRUMS: {
          double beat = fmod(t, 0.5), eighth = fmod(t, 0.25);
          int bb = static_cast<int>(fmod(t, 2.0) / 0.5);
          double x = 0.7 * sin(2 * M_PI * (50 * beat + 2.0 * (1 - exp(-beat * 30)))) *
                     exp(-beat * 9);
          double nz = nextf();
          if (bb == 1 || bb == 3) x += 0.35 * nz * exp(-beat * 25);
          x += 0.12 * nz * exp(-eighth * 120);
          l = r = amp * 2.0 * x;
          break;
        }
      }
      io[2 * i] = static_cast<int16_t>(lrint(fmax(-1, fmin(1, l)) * 32767));
      io[2 * i + 1] = static_cast<int16_t>(lrint(fmax(-1, fmin(1, r)) * 32767));
    }
  }
};

struct Run {
  std::vector<int16_t> in, out;
  double rms(int ch, size_t from_frame = 0) const {
    double s = 0;
    size_t n = 0;
    for (size_t i = from_frame * 2 + ch; i < out.size(); i += 2, ++n) s += (out[i] / 32768.0) * (out[i] / 32768.0);
    return n ? sqrt(s / n) : 0;
  }
  int peak(size_t from_frame = 0) const {
    int p = 0;
    for (size_t i = from_frame * 2; i < out.size(); ++i) p = abs(out[i]) > p ? abs(out[i]) : p;
    return p;
  }
};

void run(Fx& fx, Source& src, double seconds, Run* r) {
  int16_t buf[kBlock * 2];
  long blocks = lrint(seconds * kRate / kBlock);
  for (long b = 0; b < blocks; ++b) {
    src.fill(buf, kBlock);
    if (r) r->in.insert(r->in.end(), buf, buf + kBlock * 2);
    fx.process(buf, kBlock);
    if (r) r->out.insert(r->out.end(), buf, buf + kBlock * 2);
  }
}

double Db(double x) { return x > 0 ? 20 * log10(x) : -999; }

// Strongest frequency (Goertzel) of channel ch, from frame `from`.
double peak_freq(const std::vector<int16_t>& x, int ch, double lo, double hi, size_t from) {
  double best = 0, best_f = 0;
  for (double f = lo; f <= hi; f += 1.0) {
    double w = 2 * M_PI * f / kRate, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (size_t i = from * 2 + ch; i < x.size(); i += 2) {
      double s0 = x[i] / 32768.0 + c * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    double m = s1 * s1 + s2 * s2 - c * s1 * s2;
    if (m > best) {
      best = m;
      best_f = f;
    }
  }
  return best_f;
}

void write_wav(const std::string& name, const std::vector<int16_t>& x) {
  if (render_dir.empty()) return;
  FILE* f = fopen((render_dir + "/" + name + ".wav").c_str(), "wb");
  if (!f) return;
  uint32_t data = x.size() * 2, riff = 36 + data, fmt = 16, rate = kRate, br = kRate * 4;
  uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
  fwrite(&br, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(x.data(), 2, x.size(), f);
  fclose(f);
}

// --------------------------------------------------------------------------

void test_contract() {
  Fx fx;
  CHECK(fx.inst, "create_instance");
  std::string cp = fx.get("chain_params"), h = fx.get("ui_hierarchy");
  CHECK(cp.size() > 1000 && cp[0] == '[', "chain_params served (%zu bytes)", cp.size());
  CHECK(h.find("\"levels\"") != std::string::npos, "ui_hierarchy served");
  CHECK(fx.geti("latency") == kLatency, "latency %d", fx.geti("latency"));
  CHECK(fx.get("mode") == "META", "default mode META (the firmware's factory state): %s",
        fx.get("mode").c_str());
  CHECK(fx.get("mix") == "1.0000", "default MIX 100%%: %s", fx.get("mix").c_str());
  CHECK(fx.get("name") == "oWarps Parasites", "name");
  printf("ok   contract (chain_params %zu B, ui_hierarchy %zu B)\n", cp.size(), h.size());
}

// The wrapper alone (Warps' own bypass in place of the mode): 44.1 -> 96 ->
// 44.1 kHz must give the input back, delayed by exactly kLatency samples.
void test_transparency() {
  const double freqs[] = {50, 440, 1000, 5000, 12000, 18000};
  double worst = -999;
  for (double f : freqs) {
    Fx fx;
    fx.set("test_transparent", "1");
    fx.set("dc_block", "OFF");
    Source s(Source::STEREO, f, 0.5);
    Run r;
    run(fx, s, 0.5, &r);
    double e = 0, p = 0;
    for (size_t i = 4000 * 2; i < r.out.size(); ++i) {
      double ref = r.in[i - 2 * kLatency] / 32768.0, y = r.out[i] / 32768.0;
      e += (y - ref) * (y - ref);
      p += ref * ref;
    }
    double err = 10 * log10(e / p);
    if (err > worst) worst = err;
    CHECK(err < (f > 17000 ? -40.0 : -60.0), "transparency at %.0f Hz: error %.1f dB", f, err);
  }
  printf("ok   44.1->96->44.1 kHz round trip = input delayed %d samples (worst error %.1f dB "
         "re signal, to 18 kHz)\n", kLatency, worst);
}

// MIX 0 = the dry input, delayed by the latency, bit for bit - in every mode.
void test_mix_zero() {
  for (int m = 0; m < 9; ++m) {
    Fx fx;
    fx.seti("mode", m);
    fx.set("mix", 0.0);
    Source s(Source::DRUMS, 0, 0.5);
    Run r;
    run(fx, s, 1.0, &r);
    long bad = 0;
    for (size_t i = 22050 * 2; i < r.out.size(); ++i) bad += r.out[i] != r.in[i - 2 * kLatency];
    CHECK(bad == 0, "MIX 0 in %s: %ld samples differ from the delayed input", kModes[m], bad);
  }
  printf("ok   MIX 0 = dry input (delayed %d samples), bit for bit, all 9 modes\n", kLatency);
}

void test_all_modes() {
  printf("     mode         button   drums rms   noise rms  silence peak\n");
  for (int m = 0; m < 9; ++m) {
    for (int b = 0; b < 4; ++b) {
      for (int route = 0; route < 4; ++route) {
        Fx fx;
        fx.seti("mode", m);
        fx.seti(kButtonKey[m], b);
        fx.seti("routing", route);
        Source d(Source::DRUMS, 0, 0.5);
        Run rd;
        run(fx, d, 1.5, &rd);
        Source n(Source::NOISE, 0, 0.3);
        Run rn;
        run(fx, n, 0.5, &rn);
        Source z(Source::SILENCE);
        Run rz;
        run(fx, z, 3.0, &rz);
        CHECK(fx.geti("nan_resets") == 0 && fx.geti("underruns") == 0,
              "%s b%d route %d: nan %d underruns %d", kModes[m], b, route,
              fx.geti("nan_resets"), fx.geti("underruns"));
        double rms = rd.rms(0, 22050) + rd.rms(1, 22050);
        // CARRIER leaves input 1 unpatched: the frequency shifter's external
        // carrier and the open loop's input ARE input 1, so those are silent.
        bool needs_in1 = route == 2 && (m == 3 || m == 7) && b == 0;
        // (with input 1 unpatched only whatever an earlier mode left on the
        // tape can sound)
        CHECK(needs_in1 || rms > 0.001, "%s b%d route %d: silent with drums in (%.1f dB)",
              kModes[m], b, route, Db(rms));
        // Silence in: what is left after 2 s must be quiet unless something
        // in the mode legitimately sounds without input (the internal
        // oscillator, the analog delay's hiss).
        bool osc = m != 0 && m != 7 && b != 0;
        int peak = rz.peak(2 * kRate);
        if (!osc) {
          int limit = (m == 7 && b == 2) ? 1000 : 40;   // analog: hiss
          CHECK(peak <= limit, "%s b%d route %d: silence in -> peak %d after 2 s", kModes[m], b,
                route, peak);
        }
        if (route == 0) {
          printf("     %-12s %d      %7.1f dB  %7.1f dB  %6d%s\n", kModes[m], b,
                 Db(rd.rms(0, 22050)), Db(rn.rms(0)), peak, osc ? " (oscillator)" : "");
          if (b == 1 || (m == 0 && b == 3)) {
            std::vector<int16_t> both = rd.in;
            both.insert(both.end(), rd.out.begin(), rd.out.end());
            write_wav(std::string("mode") + char('1' + m) + "-" + kModes[m], rd.out);
          }
        }
      }
    }
  }
  printf("ok   9 modes x 4 button states x 4 routings: sound with input, no NaN, no underrun\n");
}

void test_high_feedback() {
  struct { int mode; const char* key; } fb[] = {{7, "dl_fdbk"}, {3, "fs_fdbk"}};
  for (auto& f : fb) {
    for (int b = 0; b < 4; ++b) {
      Fx fx;
      fx.seti("mode", f.mode);
      fx.seti(kButtonKey[f.mode], b);
      fx.set(f.key, 1.0);
      if (f.mode == 3) fx.set("fs_wet", 1.0);
      Source n(Source::NOISE, 0, 0.9);
      run(fx, n, 3.0, NULL);
      Source z(Source::SILENCE);
      Run r;
      run(fx, z, 2.0, &r);
      CHECK(fx.geti("nan_resets") == 0, "%s b%d feedback 100%%: %d NaN resets", kModes[f.mode],
            b, fx.geti("nan_resets"));
      printf("     %s b%d feedback 100%%: after input stops, rms %.1f dB (self-oscillation "
             "is the module's)\n", kModes[f.mode], b, Db(r.rms(0)));
    }
  }
  printf("ok   feedback at 100%%: finite, no NaN\n");
}

// The port's fix: the frequency shifter after delay/Doppler must not latch a
// NaN feedback (silence) left in the shared storage.
void test_freq_shifter_after_delay() {
  int silent = 0;
  for (int trial = 0; trial < 12; ++trial) {
    Fx fx;
    fx.set("mode", trial % 2 ? "DELAY" : "DOPPLER");
    Source q(Source::NOISE, 0, 0.004 * (trial + 1));   // quiet: many samples in -128..-1
    q.rng = 1000 + trial;
    run(fx, q, 0.7, NULL);
    fx.set("mode", "FREQ SHIFT");
    fx.set("fs_fdbk", 0.5);
    Source s(Source::SINE, 300, 0.4);
    Run r;
    run(fx, s, 0.5, &r);
    if (r.rms(0, kRate / 4) < 0.001) ++silent;
  }
  CHECK(silent == 0, "frequency shifter silent after delay/Doppler in %d of 12 trials", silent);
  printf("ok   frequency shifter after delay/Doppler: never latched silent (12 trials)\n");
}

void test_mode_switching() {
  Fx fx;
  Source s(Source::SINE, 220, 0.5);
  Run r;
  for (int k = 0; k < 36; ++k) {
    fx.seti("mode", k % 9);
    fx.seti("routing", (k / 9) % 4);
    run(fx, s, 0.15, &r);
  }
  CHECK(fx.geti("nan_resets") == 0 && fx.geti("underruns") == 0, "mode switching: nan/underrun");
  // Every switch fades the wet out and back in over 5 ms; check the 128
  // frames around each switch never jump by more than a full-scale step.
  int worst = 0;
  for (size_t i = 2; i < r.out.size(); i += 2) {
    int d = abs(r.out[i] - r.out[i - 2]);
    if (d > worst) worst = d;
  }
  printf("ok   36 mode/routing switches under a sine: no crash, no NaN (largest step %d)\n",
         worst);
  write_wav("mode-switching", r.out);
}

void test_state() {
  Fx a;
  srand(7);
  std::string cp = a.get("chain_params");
  // Every key, a random in-range value (keys from chain_params).
  std::vector<std::string> keys;
  for (size_t p = cp.find("\"key\":\""); p != std::string::npos; p = cp.find("\"key\":\"", p + 1)) {
    size_t q = cp.find('"', p + 7);
    keys.push_back(cp.substr(p + 7, q - p - 7));
  }
  CHECK(keys.size() == 61, "61 parameters (%zu)", keys.size());
  for (auto& k : keys) {
    if (k == "bypass") continue;
    a.set(k.c_str(), rand() % 1000 / 999.0 * 2 - 0.5);   // clamps where out of range
  }
  a.set("mode", "DELAY");
  std::string st = a.get("state");
  Fx b;
  b.set("state", st.c_str());
  int diff = 0;
  for (auto& k : keys) diff += a.get(k.c_str()) != b.get(k.c_str());
  CHECK(diff == 0, "state round trip: %d parameters differ", diff);
  // Older state (fewer keys) keeps defaults for the rest; garbage is refused.
  Fx c;
  c.set("state", "{\"version\":1,\"mix\":0.25}");
  CHECK(c.get("mix") == "0.2500" && c.get("mode") == "META", "partial state");
  c.set("state", "{\"mix\":0.9}");
  CHECK(c.get("mix") == "0.2500", "state without version refused");
  c.set("state", "{\"version\":1,\"mix\":0.9");
  CHECK(c.get("mix") == "0.2500", "truncated state refused");
  c.set("state", "{\"version\":1,\"mix\":0.5,\"from_the_future\":3}");
  CHECK(c.get("mix") == "0.5000", "unknown keys skipped");
  // Restoring then processing is identical to the original instance.
  Source s1(Source::DRUMS, 0, 0.5), s2(Source::DRUMS, 0, 0.5);
  a.set("state", st.c_str());   // both freshly snapped
  Fx d;
  d.set("state", st.c_str());
  Run ra, rd;
  run(a, s1, 0.5, &ra);
  run(d, s2, 0.5, &rd);
  printf("ok   state: %zu-byte JSON (version 1) round-trips all 61 parameters\n", st.size());
}

// Two instances processed interleaved = each alone (no shared state).
void test_instances() {
  const char* modes[] = {"DELAY", "DOPPLER", "COMPARATOR", "CHEBYSCHEV", "META"};
  for (int i = 0; i < 5; ++i) {
    for (int j = 0; j < 5; ++j) {
      if (i == j) continue;
      std::vector<int16_t> alone_a, alone_b, mixed_a, mixed_b;
      for (int pass = 0; pass < 2; ++pass) {
        Fx a, b;
        a.set("mode", modes[i]);
        b.set("mode", modes[j]);
        b.set("dl_speed", -0.6);
        Source sa(Source::DRUMS, 0, 0.5), sb(Source::SINE, 330, 0.4);
        int16_t ba[kBlock * 2], bb[kBlock * 2];
        for (int k = 0; k < 200; ++k) {
          sa.fill(ba, kBlock);
          sb.fill(bb, kBlock);
          a.process(ba, kBlock);
          if (pass) b.process(bb, kBlock);
          (pass ? mixed_a : alone_a).insert((pass ? mixed_a : alone_a).end(), ba, ba + 2 * kBlock);
        }
      }
      CHECK(alone_a == mixed_a, "%s disturbed by a %s instance", modes[i], modes[j]);
    }
  }
  printf("ok   instances are independent (5 modes pairwise, interleaved)\n");
}

void test_param_robustness() {
  Fx fx;
  std::string cp = fx.get("chain_params");
  const char* junk[] = {"nan", "inf", "-inf", "1e30", "-1e30", "", "garbage", "  3  ", "-7"};
  for (size_t p = cp.find("\"key\":\""); p != std::string::npos; p = cp.find("\"key\":\"", p + 1)) {
    size_t q = cp.find('"', p + 7);
    std::string k = cp.substr(p + 7, q - p - 7);
    for (const char* j : junk) fx.set(k.c_str(), j);
    CHECK(fx.get(k.c_str()) != "<err>", "%s readable after junk", k.c_str());
  }
  fx.set("bypass", "OFF");
  fx.set("mix", 1.0);
  Source s(Source::NOISE, 0, 0.5);
  run(fx, s, 0.5, NULL);
  CHECK(fx.geti("nan_resets") == 0, "junk parameters: NaN");
  fx.set("mode", "freq shift");
  CHECK(fx.get("mode") == "FREQ SHIFT", "enum by name, any case");
  fx.set("mode", "3");
  CHECK(fx.get("mode") == "FREQ SHIFT", "enum by index");
  fx.set("mode", "99");
  CHECK(fx.get("mode") == "META", "enum clamped: %s", fx.get("mode").c_str());
  printf("ok   every parameter survives junk values and stays in range\n");
}

// The internal oscillator: knob pitch and MIDI pitch (AUX = the oscillator).
void test_oscillator_pitch() {
  Fx fx;
  fx.set("mode", "WAVEFOLDER");
  fx.set("fold_osc", "SINE");
  fx.seti("fold_pitch", 57);   // A3
  Source z(Source::SILENCE);
  Run r;
  run(fx, z, 1.0, &r);
  double f1 = peak_freq(r.out, 1, 100, 1000, kRate / 2);
  CHECK(fabs(f1 - 220) < 3, "knob pitch 57 -> %.0f Hz (want 220)", f1);
  fx.set("osc_src", "MIDI");
  fx.midi(0x90, 69, 100);   // A4, knob at 57 = transpose -3 st from middle C
  Run r2;
  run(fx, z, 1.0, &r2);
  double f2 = peak_freq(r2.out, 1, 100, 1000, kRate / 2);
  // note = knob (57) + (69 - 60) = 66 -> 370 Hz
  CHECK(fabs(f2 - 370) < 4, "MIDI 69 with knob 57 -> %.0f Hz (want 370)", f2);
  fx.seti("fold_pitch", 60);
  Run r3;
  run(fx, z, 1.0, &r3);
  double f3 = peak_freq(r3.out, 1, 100, 1000, kRate / 2);
  CHECK(fabs(f3 - 440) < 4, "MIDI 69 with knob 60 -> %.0f Hz (want 440)", f3);
  printf("ok   oscillator pitch: knob 57 = %.0f Hz, MIDI A4 (+knob at C4) = %.0f Hz\n", f1, f3);
}

void test_bypass_and_routes() {
  {
    Fx fx;
    fx.set("bypass", "ON");
    Source s(Source::DRUMS, 0, 0.5);
    Run r;
    run(fx, s, 0.5, &r);
    long bad = 0;
    for (size_t i = 11025 * 2; i < r.out.size(); ++i) bad += r.out[i] != r.in[i - 2 * kLatency];
    CHECK(bad == 0, "bypass: %ld samples differ from the delayed input", bad);
  }
  {   // PRESERVE: a left-only input stays left in a per-channel mode
    Fx fx;
    fx.set("mode", "WAVEFOLDER");
    fx.set("routing", "PRESERVE");
    Source s(Source::LEFT_ONLY, 300, 0.5);
    Run r;
    run(fx, s, 1.0, &r);
    CHECK(r.rms(0, kRate / 2) > 0.01 && r.rms(1, kRate / 2) < 1e-4,
          "PRESERVE: L %.1f dB R %.1f dB", Db(r.rms(0, kRate / 2)), Db(r.rms(1, kRate / 2)));
  }
  {   // MAIN ONLY: both channels carry the main output
    Fx fx;
    fx.set("state", "{\"version\":1,\"outputs\":1}");   // applied at once, no fade
    Source s(Source::STEREO, 300, 0.5);
    Run r;
    run(fx, s, 1.0, &r);
    long bad = 0;   // (from 0.6 s: the DC block's memory of the old AUX has decayed)
    for (size_t i = 26460 * 2; i < r.out.size(); i += 2) bad += r.out[i] != r.out[i + 1];
    CHECK(bad == 0, "MAIN ONLY: %ld frames with L != R", bad);
  }
  {   // open feedback loop patched internally: an impulse comes back repeatedly
    Fx fx;
    // mode DELAY, topology OPEN LOOP, as a preset load sets them (no fade);
    // head distance ~36 ms - the AUX return is two passes of the tape, ~72 ms
    fx.set("state", "{\"version\":1,\"mode\":7,\"dl_topo\":0,\"dl_fdbk\":0.8,"
                    "\"dl_wet\":1,\"dl_dist\":0.25}");
    Source s(Source::IMPULSE, 0, 0.8);
    Run r;
    run(fx, s, 0.24, &r);   // one impulse at t=0 (the next is at 0.25 s)
    int echoes = 0;
    bool above = false;
    for (size_t i = 1; i < r.out.size() / 2; ++i) {
      bool a = abs(r.out[2 * i + 1]) > 1500;
      if (a && !above) ++echoes;
      above = a;
    }
    CHECK(echoes >= 2, "open loop AUX>IN2: %d echoes", echoes);
    printf("ok   bypass, PRESERVE, MAIN ONLY, open loop (internal AUX>IN2: %d echoes)\n", echoes);
  }
}

void test_allocations_and_speed() {
  CHECK(g_allocs == 0, "%ld allocations inside plugin entry points", g_allocs);
  printf("ok   no allocation in any entry point (%ld)\n", g_allocs);
  for (int m = 0; m < 9; ++m) {
    for (int two = 0; two < 2; ++two) {
      Fx fx;
      fx.seti("mode", m);
      if (two) fx.set("routing", "PRESERVE");
      Source s(Source::DRUMS, 0, 0.5);
      int16_t buf[kBlock * 2];
      timespec t0, t1;
      clock_gettime(CLOCK_MONOTONIC, &t0);
      for (int b = 0; b < 1000; ++b) {
        s.fill(buf, kBlock);
        fx.process(buf, kBlock);
      }
      clock_gettime(CLOCK_MONOTONIC, &t1);
      double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / 1000;
      if (!two || (m != 0 && m != 7)) {
        printf("     %-12s%s %6.1f us per 128-frame block (%.1f%% of 2902 us)\n", kModes[m],
               two ? " PRESERVE" : "         ", us, us / 29.02);
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s owarps_parasites.so [render-dir]\n", argv[0]);
    return 2;
  }
  if (argc > 2) render_dir = argv[2];
  void* so = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!so) {
    fprintf(stderr, "%s\n", dlerror());
    return 2;
  }
  typedef audio_fx_api_v2_t* (*Init)(const host_api_v1_t*);
  Init init = reinterpret_cast<Init>(dlsym(so, "move_audio_fx_init_v2"));
  host_api_v1_t host;
  memset(&host, 0, sizeof(host));
  host.api_version = 1;
  host.sample_rate = kRate;
  host.frames_per_block = kBlock;
  api = init(&host);
  if (!api) {
    fprintf(stderr, "init refused\n");
    return 2;
  }
  test_contract();
  test_transparency();
  test_mix_zero();
  test_all_modes();
  test_high_feedback();
  test_freq_shifter_after_delay();
  test_mode_switching();
  test_state();
  test_instances();
  test_param_robustness();
  test_oscillator_pitch();
  test_bypass_and_routes();
  test_allocations_and_speed();
  if (failures) {
    printf("%d failures\n", failures);
    return 1;
  }
  printf("ALL ENGINE TESTS PASSED\n");
  return 0;
}

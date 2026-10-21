// oWarps Parasites - Schwung audio FX v2 adapter.
//
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
//
// Every entry point here runs on Move's SPI callback (or, for a bus insert,
// the bus worker): no allocation, no file I/O, no locks. Instances come from
// a static pool claimed with an atomic compare-and-swap, so even
// create_instance allocates nothing.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <new>

// The vendored Schwung header is C and checks its own layout with
// _Static_assert, which C++ spells static_assert. Same check, same header.
#define _Static_assert static_assert
#include "host/audio_fx_api_v2.h"
#undef _Static_assert
#include "owarps_metadata.h"
#include "owarps_params.h"
#include "wrapper/engine.h"

using owarps::Engine;
using owarps::kParams;
using owarps::P_COUNT;

namespace {

const int kPoolSize = 8;
const int kStateVersion = 1;

struct Slot {
  int used;
  alignas(16) unsigned char storage[sizeof(Engine)];
};

Slot g_pool[kPoolSize];
int g_host_rate = 44100;

inline Engine* engine_of(void* p) { return reinterpret_cast<Engine*>(p); }

int param_index(const char* key) {
  for (int i = 0; i < P_COUNT; ++i) {
    if (!strcmp(key, kParams[i].key)) return i;
  }
  return -1;
}

const char* skip_space(const char* p) {
  while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
  return p;
}

// A value is a number, or (enums) an option name in any case.
bool parse_value(int index, const char* text, float* out) {
  const owarps::ParamDef& d = kParams[index];
  const char* p = skip_space(text);
  for (int i = 0; i < d.n_options; ++i) {
    size_t n = strlen(d.options[i]);
    if (!strncasecmp(p, d.options[i], n) && !*skip_space(p + n)) {
      *out = static_cast<float>(i);
      return true;
    }
  }
  char* end;
  float v = strtof(p, &end);
  if (end == p || *skip_space(end) || !isfinite(v)) return false;
  *out = v;
  return true;
}

// Flat JSON object of numbers with a "version". Keys this build does not know
// (a later version's) are skipped; parameters the state does not carry (an
// earlier version's) keep their defaults. Nothing is applied unless the
// whole object parses.
bool restore_state(Engine* e, const char* text) {
  if (strlen(text) > 8192) return false;
  float next[P_COUNT];
  for (int i = 0; i < P_COUNT; ++i) next[i] = kParams[i].def;
  bool have_version = false;
  const char* p = skip_space(text);
  if (*p++ != '{') return false;
  for (int field = 0; field < 256; ++field) {
    p = skip_space(p);
    if (*p++ != '"') return false;
    char key[32];
    int n = 0;
    while (*p && *p != '"' && n < static_cast<int>(sizeof(key)) - 1) key[n++] = *p++;
    key[n] = 0;
    if (*p++ != '"') return false;
    p = skip_space(p);
    if (*p++ != ':') return false;
    p = skip_space(p);
    char* end;
    float v = strtof(p, &end);
    if (end == p || !isfinite(v)) return false;
    p = end;
    int idx = param_index(key);
    if (idx >= 0) {
      next[idx] = v;
    } else if (!strcmp(key, "version")) {
      if (v < 1.0f) return false;
      have_version = true;
    }
    p = skip_space(p);
    if (*p == '}') {
      if (*skip_space(p + 1) || !have_version) return false;
      for (int i = 0; i < P_COUNT; ++i) e->SetParam(i, next[i]);
      e->Snap();
      return true;
    }
    if (*p++ != ',') return false;
  }
  return false;
}

int copy_text(char* buf, int size, const char* text) {
  size_t n = strlen(text);
  if (n >= static_cast<size_t>(size)) {
    buf[0] = 0;
    return -1;
  }
  memcpy(buf, text, n + 1);
  return static_cast<int>(n);
}

void* create_instance(const char* module_dir, const char* config_json) {
  (void)module_dir;
  (void)config_json;
  for (int i = 0; i < kPoolSize; ++i) {
    int expected = 0;
    if (__atomic_compare_exchange_n(&g_pool[i].used, &expected, 1, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
      // Zeroed first, as the firmware's objects are (static storage): some
      // Warps members are never written by Init() and start from zero on
      // the module.
      memset(g_pool[i].storage, 0, sizeof(g_pool[i].storage));
      Engine* e = new (g_pool[i].storage) Engine();
      if (!e->Init(g_host_rate)) {
        __atomic_store_n(&g_pool[i].used, 0, __ATOMIC_RELEASE);
        return NULL;
      }
      return e;
    }
  }
  return NULL;
}

void destroy_instance(void* instance) {
  for (int i = 0; i < kPoolSize; ++i) {
    if (instance == g_pool[i].storage) {
      __atomic_store_n(&g_pool[i].used, 0, __ATOMIC_RELEASE);
      return;
    }
  }
}

void process_block(void* instance, int16_t* audio, int frames) {
  if (!instance || !audio || frames <= 0) return;
  engine_of(instance)->Process(audio, frames);
}

void set_param(void* instance, const char* key, const char* val) {
  if (!instance || !key || !val) return;
  Engine* e = engine_of(instance);
  if (!strcmp(key, "state")) {
    (void)restore_state(e, val);
    return;
  }
  if (!strcmp(key, "test_transparent")) {   // tests only: wrapper without Warps
    e->set_test_transparent(strtof(val, NULL) != 0.0f);   // (no strtol: glibc 2.38 renames it)
    return;
  }
  int idx = param_index(key);
  float v;
  if (idx < 0 || !parse_value(idx, val, &v)) return;
  e->SetParam(idx, v);
}

int format_value(const Engine* e, int idx, char* buf, int size) {
  float v = e->GetParam(idx);
  const owarps::ParamDef& d = kParams[idx];
  if (d.type == owarps::TYPE_ENUM) return snprintf(buf, size, "%s", d.options[static_cast<int>(v)]);
  if (d.type == owarps::TYPE_INT) return snprintf(buf, size, "%d", static_cast<int>(v));
  return snprintf(buf, size, "%.4f", v);
}

int write_state(const Engine* e, char* buf, int size) {
  int n = snprintf(buf, size, "{\"version\":%d", kStateVersion);
  for (int i = 0; i < P_COUNT && n >= 0 && n < size; ++i) {
    n += kParams[i].type == owarps::TYPE_FLOAT
        ? snprintf(buf + n, size - n, ",\"%s\":%.6g", kParams[i].key, e->GetParam(i))
        : snprintf(buf + n, size - n, ",\"%s\":%d", kParams[i].key,
                   static_cast<int>(e->GetParam(i)));
  }
  if (n >= 0 && n < size) n += snprintf(buf + n, size - n, "}");
  return n;
}

int get_param(void* instance, const char* key, char* buf, int size) {
  if (!instance || !key || !buf || size <= 0) return -1;
  buf[0] = 0;
  const Engine* e = engine_of(instance);
  int n = -1;
  int idx = param_index(key);
  if (idx >= 0) {
    n = format_value(e, idx, buf, size);
  } else if (!strcmp(key, "name")) {
    return copy_text(buf, size, "oWarps Parasites");
  } else if (!strcmp(key, "chain_params")) {
    return copy_text(buf, size, kChainParams);
  } else if (!strcmp(key, "ui_hierarchy")) {
    return copy_text(buf, size, kHierarchy);
  } else if (!strcmp(key, "state")) {
    n = write_state(e, buf, size);
  } else if (!strcmp(key, "version")) {
    return copy_text(buf, size, kVersion);
  } else if (!strcmp(key, "latency")) {
    n = snprintf(buf, size, "%d", e->latency_samples());
  } else if (!strcmp(key, "nan_resets")) {
    n = snprintf(buf, size, "%d", e->nan_resets());
  } else if (!strcmp(key, "underruns")) {
    n = snprintf(buf, size, "%d", e->underruns());
  } else if (!strcmp(key, "applied_mode")) {
    n = snprintf(buf, size, "%d", e->applied_mode());
  }
  if (n < 0 || n >= size) {
    buf[0] = 0;
    return -1;
  }
  return n;
}

// Notes set the internal oscillator's pitch when OSC PITCH SOURCE is MIDI.
void on_midi(void* instance, const uint8_t* msg, int len, int source) {
  (void)source;
  if (!instance || !msg || len < 3) return;
  if ((msg[0] & 0xf0) == 0x90 && msg[2] > 0 && msg[1] < 128) {
    engine_of(instance)->NoteOn(msg[1]);
  }
}

audio_fx_api_v2_t g_api = {
  AUDIO_FX_API_VERSION_2,
  create_instance,
  destroy_instance,
  process_block,
  set_param,
  get_param,
  on_midi,
};

}  // namespace

extern "C" __attribute__((visibility("default")))
audio_fx_api_v2_t* move_audio_fx_init_v2(const host_api_v1_t* host) {
  if (host && host->sample_rate > 0) {
    if (host->sample_rate != 44100) return NULL;
    g_host_rate = host->sample_rate;
  }
  return &g_api;
}

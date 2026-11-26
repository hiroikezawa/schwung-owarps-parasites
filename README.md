# oWarps Parasites

**English** | [日本語](README.ja.md)

An Audio FX module for Schwung on Ableton Move. It runs the 9 modes of
**Warps Parasites**, Matthias Puech's custom firmware for Mutable Instruments
Warps, **with the original DSP code as it is**, at Warps' own 96 kHz.

- Module ID: `owarps_parasites` (the DSP is `owarps_parasites.so`)
- Type: `audio_fx` (usable in a track slot, in Master FX and as a bus insert)
- Version: 0.1.0
- Author: Hiro Ikezawa

This is not a new Warps-like effect. It takes in the Parasites source
(`warps/` in https://github.com/mqtthiqs/parasites), and tests confirm that
**its output is bit-identical to the original firmware's** (see "Tests" below).

## The 9 modes

| # | Mode | What it is |
|---|---|---|
| 1 | DOPPLER | A binaural Doppler panner: the source's X/Y position, the interaural time difference (±1.5 ms), distance attenuation, and the Doppler pitch shift that comes from real delay lines changing length. An LFO makes the source circle around. Inputs 1 and 2 sit on opposite sides of the head. |
| 2 | WAVEFOLDER | A wavefolder (Warps' fold table). FOLD = fold amount, BIAS = input bias. |
| 3 | CHEBYSCHEV | A waveshaper built from Chebyshev polynomials. ORDER = order (0–16, interpolated continuously between orders), GAIN. |
| 4 | FREQ SHIFT | A frequency shifter (Hilbert transform + quadrature oscillator). SHIFT = shift amount (positive or negative), UP/DN = crossfade between upper and lower sidebands, FDBK = feedback, DRY/WT = internal dry/wet. |
| 5 | BIT MANGLER | Two-input bit degradation. DEGRD = bit degradation, XMOD = morph through sum → OR → XOR → shift. AUX is degraded too. |
| 6 | COMPARATOR | A morph through 8 comparison functions (starting from the sum of both inputs) + a Chebyshev waveshaper. COMPAR = comparison function, ORDER = order. |
| 7 | VOCODER | A 20-band vocoder (Warps' filter bank). WARP = formant warp, RELEAS = release time. Input 1 is the carrier, input 2 the modulator. |
| 8 | DELAY | A variable-speed tape delay. SPEED = tape speed and direction (+100 % is fastest forwards, 0 stops, negative runs backwards), DIST = distance between the read and write heads, FDBK = feedback, DRY/WT. TOPO picks one of 4 topologies (below). |
| 9 | META | The original Warps' main mode (a continuous morph through crossfade → fold → analog ring modulation → digital ring modulation → XOR → comparator → vocoder). ALGO and TIMBRE. |

### Knobs (Move's 8 knobs)

The screen shows **only the page of the current mode**. In every mode
**knob 1 = MIX** and **knob 8 = MODE**. Turning MODE switches to that mode's page.

| Mode | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| DOPPLER | MIX | X | Y | LFO RT | LFO AM | ROOM | OUT | MODE |
| WAVEFOLDER | MIX | FOLD | BIAS | LEVEL1 / PITCH | LEVEL2 | OSC | OUT | MODE |
| CHEBYSCHEV | MIX | ORDER | GAIN | LEVEL1 / PITCH | LEVEL2 | OSC | OUT | MODE |
| FREQ SHIFT | MIX | SHIFT | UP/DN | FDBK | DRY/WT | CARR | OUT | MODE |
| BIT MANGLER | MIX | DEGRD | XMOD | LEVEL1 / PITCH | LEVEL2 | OSC | OUT | MODE |
| COMPARATOR | MIX | COMPAR | ORDER | LEVEL1 / PITCH | LEVEL2 | OSC | OUT | MODE |
| VOCODER | MIX | WARP | RELEAS | LEVEL1 / PITCH | LEVEL2 | OSC | OUT | MODE |
| DELAY | MIX | SPEED | DIST | FDBK | DRY/WT | TOPO | OUT | MODE |
| META | MIX | ALGO | TIMBRE | LEVEL1 / PITCH | LEVEL2 | OSC | OUT | MODE |

- Knob 2 is Warps' big knob (ALGORITHM), knob 3 the small knob (TIMBRE), knobs 4
  and 5 the LEVEL 1 and 2 pots, and knob 6 Warps' button (its meaning differs per
  mode).
- Every value is saved as a Warps pot position and goes through the same pot
  curve (ALGORITHM's detents) and smoothing (1600 times a second) as the firmware.
- Each mode's settings are saved separately. Switching modes and coming back
  restores the previous settings. (On the real Warps the button state is shared
  by all modes; here it is per mode.)
- Turn the jog wheel for the **Settings** page: ROUTE, OUTPUT, IN, PITCH (pitch
  source), PATCH, DC CUT, BYPASS.

Differences from the specification's "initial parameter assignment": "Input
Level", "Input Balance" and "Carrier Level", which do not exist on Warps itself,
are mapped to Warps' real controls LEVEL 1 / LEVEL 2 (the levels of inputs 1 and
2; in the vocoder input 1 = carrier). Doppler's LFO amount, the frequency
shifter's dry/wet, the delay's dry/wet and topology, and Warps' button are on
knobs 5–6 of the same page.

### Internal oscillator

When OSC (knob 6) is anything other than EXT, Warps' internal oscillator
becomes the carrier in place of input 1 (WAVEFOLDER / CHEBYSCHEV / BIT MANGLER /
COMPARATOR: SINE, TRIANGLE, SAW; VOCODER: SAW, PULSE, NOISE; META: different
waveforms for the cross-modulation side and the vocoder side; FREQ SHIFT: the
SINE, HARMONIC and BUZZY quadrature wavetables).

- **Input 1 then becomes the oscillator's phase-modulation input** (as in the original).
- On the hardware the LEVEL 1 pot doubles as the oscillator's pitch. Here, with
  the oscillator on, knob 4 switches to **PITCH** (MIDI note 36–96, the range of
  the hardware's LEVEL 1 pot).
- With Settings → PITCH set to **MIDI**, notes played on that track set the pitch
  (transposed with the knob's PITCH as middle C; the same tracking as the
  firmware uses for LEVEL 1's V/Oct CV: jumps over 0.4 semitone at once, smaller
  ones smoothly). MIDI arrives in a track slot. Schwung 1.7.3's Master FX
  receives no MIDI, so in Master FX only the knob's pitch is available.

### DELAY topologies (TOPO)

| TOPO | What it is |
|---|---|
| OPEN LOOP | On the hardware, the mode where a patch cable makes AUX → IN 2 an external loop (below). |
| DUAL | A dual delay whose two channels share one delay time. |
| ANALOG | Analog-style, with a filter, light distortion and hiss in the feedback. |
| PING-PONG | Each channel's feedback goes to the other side. |

**OPEN LOOP and the external patch**: on the hardware IN 1 is the input, OUT the
output, AUX the feedback send and IN 2 the feedback return, and you patch any
module between them. Move cannot patch externally, so you choose with Settings
→ **PATCH**.

- `AUX>IN2` (default): an internal loop returning AUX straight to IN 2. The same
  as joining AUX and IN 2 with one cable on the hardware, except that the loop
  has one Warps block (60 samples, 0.625 ms) of delay (not the same as the
  hardware codec's round-trip delay).
- `IN 2`: IN 2 is the input's right channel as usual (following ROUTE). For when
  you want some other sound in IN 2 instead of an external loop.

## Inputs, outputs and routing (Settings → ROUTE / OUTPUT)

| ROUTE | Input 1 | Input 2 | Output |
|---|---|---|---|
| DUAL (default) | Left | Right | Left = MAIN, right = AUX |
| MONO | (L+R)/2 | (L+R)/2 | As above |
| CARRIER | Unplugged (silent) | (L+R)/2 | As above |
| PRESERVE | One independent Warps each for left and right | | Left = left Warps' MAIN, right = right Warps' MAIN |

- **DUAL**: Warps' own two inputs and two outputs. The left and right material are
  combined as "two separate signals". The right output is AUX, so in some modes it
  carries something close to the dry signal or the oscillator (how the original's
  AUX works: for example WAVEFOLDER's AUX is the sum of the inputs, or the
  oscillator when it is in use). To get MAIN on both, set OUTPUT to **MAIN ONLY**.
- **CARRIER**: the material goes only into input 2 (the modulator) and input 1 is
  left empty. With OSC on, this becomes "modulate the material with the internal
  oscillator". FREQ SHIFT (CARR = IN 1) and DELAY's OPEN LOOP, where input 1 is
  the carrier or the input itself, have no input in this setting and are silent.
- **PRESERVE**: for keeping stereo placement. It uses two Warps (CPU about
  1.3–1.6× as measured on a PC) and processes each channel independently. With
  OSC, each channel is input 2 and input 1 is unplugged. **DOPPLER and DELAY stay
  a single DUAL even in PRESERVE** (in those two the coupling of two inputs and
  two outputs is itself the placement/stereo, so splitting them changes the meaning).

Mono material: a Move track's audio is the same signal on both sides, so even in
DUAL both inputs get the same sound (the same result as MONO).

**IN** (input gain): default 0 dB (Move's full scale = Warps' input full scale,
16 Vpp). For the equivalent of Eurorack's 10 Vpp use about -4 dB.

## MIX

- Knob 1. A linear crossfade from 0 to 100 % (with about 10 ms of smoothing).
- 0 % = dry (the input itself), 100 % = wet only. Default 100 %.
- FREQ SHIFT's and DELAY's internal dry/wet (the hardware's LEVEL 2) are kept as
  separate knobs, and MIX is the final stage after them.
- The wet side goes through resampling 44.1 kHz → 96 kHz → 44.1 kHz and Warps'
  processing in 60-sample units, so it is **103 samples (2.34 ms)** late overall.
  The filters are designed so that this round trip is **exactly a whole number of
  samples**, and the dry is delayed by the same 103 samples, so no cancellation
  from phase offset (comb filtering) happens anywhere along MIX. The output at
  MIX 0 % is bit-identical to the input delayed by 103 samples (checked in tests).
- OUT (knob 7) is the wet level (-24 to +12 dB). It has no effect at MIX 0 %.

## DC CUT

Warps' outputs are capacitor-coupled (AC-coupled), so the hardware puts out no
DC. The DSP itself, however, puts out DC even for silent input in CHEBYSCHEV
(even-order polynomials), BIT MANGLER (bitwise OR), COMPARATOR and others. So a
first-order 2 Hz high-pass, standing in for the hardware's output stage, is
applied to the wet (Settings → DC CUT, default ON). With it OFF you get the DSP's
output as it is.

## Bypass and click prevention

- **BYPASS** (Settings): when ON the output crossfades to the dry (the same as
  MIX 0 %, 103 samples late) over about 10 ms. Schwung's own bypass (Mute + jog
  click) also works.
- Changes of MODE, ROUTE, OUTPUT, the button (OSC / ROOM / TOPO / CARR) and PATCH
  fade the wet out over 5 ms → switch → fade back in over 5 ms (the hardware does
  not fade on a switch, but this was added so Move's output does not click).
- Nothing is added to continuous knob movements such as tape speed, direction,
  head distance and feedback. The firmware's own smoothing (0.007 per sample for
  tape speed, about 0.5 s for head distance) is Parasites' tape behaviour as it is.

## Every difference from the original

Only these differences change the DSP's output. Everything else is bit-identical
to the original.

1. **Sample-rate conversion**: Warps runs at 96 kHz, Move at 44.1 kHz. The inside
   runs at 96 kHz as it is, with high-quality polyphase resamplers before and
   after (passband to 19.5 kHz, stopband 88–93 dB). The resamplers alone have an
   error of -95 dB relative to the signal up to 18 kHz. Above 19.5 kHz the band
   is limited.
2. **Delay reading past the end of the tape**: right after a sudden change from a
   slow to a fast speed and the like, the read position goes up to about 10
   samples past the end of the ring buffer, and the original reads memory outside
   the buffer (on the hardware, whatever is in the neighbouring RAM). That cannot
   be reproduced, so it wraps to the start of the ring (as intended).
3. **The frequency shifter stuck on NaN**: entering the frequency shifter after
   Delay / Doppler, a value in the region shared with the delay lines is used as
   the feedback sample, and if it happens to be a NaN pattern it can freeze in
   silence (on the hardware, until power-off). Entering the frequency shifter now
   starts the feedback from 0.
4. **Start-up at power-on**: right after power-on the hardware's tape starts from
   a stop and the head distance glides from 0 to its setting over about 0.5 s. On
   Move an instance is created every time a set opens, so creation and preset
   loads start in the "already running" state.
5. **Bit Mangler's negative shift**: when the shift amount of `x << (y >> 12)` is
   negative, the Cortex-M4 (hardware) returns 0 while Move's AArch64 shifts by 24
   bits. The hardware's result is computed explicitly.
6. **DC cut on the output** (above; it can be switched off) and **5 ms fades on
   mode and similar switches**.
7. **Input normalisation detection**: the hardware zeroes an input when nothing is
   plugged into its jack. Here inputs are always treated as "plugged" (only
   CARRIER's input 1 is treated as unplugged). CV inputs are treated as "nothing
   plugged" (each CV contributes 0, and the LEVEL CV jacks are unplugged = the pot
   alone sets the level).

## Tests

`scripts/test.sh` runs all of the following (on this machine).

| Test | What | Result |
|---|---|---|
| Provenance | The source at the pinned commit is fetched again, and with the patches applied it matches `src/mi` exactly | PASS |
| **Against the original** | The original Parasites source (unmodified `modulator.cc` etc. and the firmware's `cv_scaler.cc`) and the port are linked into the same program and compared over 9 modes × 4 button states × 5 knob settings (centre, all 0, all maximum, skewed, every knob swept over its full range) × 5 input sets (sine, saw, square, white noise, impulse, silence, drum loop) = **900 scenarios, 57.6 million samples per side** | **Bit-identical in every scenario** |
| Resampler | Round-trip error of the wrapper alone (Warps bypassed), 50 Hz – 18 kHz | at most -95.3 dB, 103 samples of latency |
| MIX 0% | In all 9 modes, output = the input delayed by 103 samples | Bit-identical |
| Every mode works | 9 modes × 4 buttons × 4 routings: sounds with drums, no NaN, no underruns, no runaway oscillation with silent input | PASS |
| High feedback | Feedback 100 % in the 4 DELAY topologies and the frequency shifter + full-scale noise | No NaN (self-oscillation is the module's own behaviour) |
| Mode switching | 36 mode and routing switches during a sine input | No crash, no NaN |
| Saved state | All 61 parameters saved and restored as JSON (version 1), handling of old/unknown keys, rejection of broken JSON | PASS |
| Multiple instances | Over combinations of 5 modes, two instances processed alternately give the same output as one alone | PASS |
| Abnormal values | nan / inf / 1e30 / empty string / text for every parameter | Kept within range |
| Oscillator pitch | PITCH 57 = 220 Hz, MIDI A4 (PITCH 60) = 440 Hz | PASS |
| Bypass / PRESERVE / MAIN ONLY / OPEN LOOP | | PASS |
| Realtime safety | Number of malloc/free calls in every entry point | 0 |
| ASan + UBSan | The whole engine test above | PASS |
| UI | With the same page engine as a real Schwung (the device's `shared/`): the 9 modes' pages, knob 1 = MIX, page switching with MODE, PITCH replacing LEVEL1 with OSC, Settings | PASS |
| ARM64 build | AArch64, glibc 2.27 or older, a single exported symbol, no reference to malloc/new/mutex | PASS |
| Licence | An MIT notice in every imported file, no copyleft wording | PASS |

For the comparison to match, the reference side (the original) gets only these
three same fixes. Each aligns the answer with "the module's answer" rather than
"this build machine's answer to undefined behaviour"; without them the comparison
itself would be meaningless: Bit Mangler's negative shift (the Cortex-M4 result),
Delay's read past the end of the tape (the original reads outside the buffer, so
the value read differs per build), and float→int conversions of NaN and
out-of-range values that only x86 does differently (saturating as on ARM). When
run on Move (AArch64) the third is not applied.

### Done on the device (Move): `scripts/bench_move.sh`

Runs the ARM64 comparison test (the same 900 scenarios as above) and a load
measurement on the device (mean, 99th percentile and maximum processing time over
9 modes × 4 buttons × DUAL/PRESERVE, memory, creation time) on the Move.

**Results on Move (2026-10-22)**

- ARM64 comparison test: all 900 scenarios bit-identical to the original.
- Mean processing time per block (128 frames, budget 2902 µs) (normal priority,
  cores 0–2, drum-loop input, knob 2 swept):

| Mode | DUAL | PRESERVE |
|---|---|---|
| DOPPLER | 245–304 µs (8.5–10.5 %) | (same processing as DUAL) 253–332 µs |
| WAVEFOLDER | 289–317 µs (10–11 %) | 380–472 µs (13–16 %) |
| CHEBYSCHEV | 352–388 µs (12–13 %) | 502–562 µs (17–19 %) |
| FREQ SHIFT | 280–318 µs (10–11 %) | 350–443 µs (12–15 %) |
| BIT MANGLER | 220–244 µs (8 %) | 282–307 µs (10–11 %) |
| COMPARATOR | 330–376 µs (11–13 %) | 450–485 µs (16–17 %) |
| VOCODER | 396–468 µs (14–16 %) | 627–647 µs (22 %) |
| DELAY | 216–265 µs (7.5–9 %) | (same processing as DUAL) 219–259 µs |
| META | 333–361 µs (12 %) | 462–488 µs (16–17 %) |

  The heaviest is **VOCODER in PRESERVE (mean 647 µs, 22 %)**. The maxima (around
  1–3 ms, once 11.8 ms) were measured at normal priority, so they include
  interruptions by Move's own processing. 0 underruns, 0 NaN resets.
- Memory: RSS +460 kB on loading the library, +372 kB with one instance running.
- create_instance 0.89 ms, preset load 0.05 ms.
- Installed (md5 matches). Operation on screen and listening are not yet checked.

## Building and installing

Requirements: Linux or Windows WSL2, `g++`, `aarch64-linux-gnu-g++`
(`g++-aarch64-linux-gnu` on Debian/Ubuntu), Python 3, git. The UI tests need
Node.js and a Schwung checkout (`../schwung`).

```bash
scripts/build.sh arm64
```

This produces `dist/owarps_parasites-module.tar.gz`. Install through Schwung Manager:

```bash
python3 scripts/install.py http://move.local:7700
```

Restart Move after installing.

Tests:

```bash
scripts/test.sh
```

```bash
scripts/bench_move.sh ableton@move.local
```

## Files

| | |
|---|---|
| `src/mi/` | Parasites' Warps DSP and stmlib (imported, `patches/` applied) |
| `src/wrapper/engine.*` | Resampling, blocking, routing, MIX, fades, DC cut |
| `src/wrapper/cv_reader.h` | A transcription of the firmware's pot reading (`cv_scaler.cc`) |
| `src/wrapper/resampler.h` | Polyphase resampler |
| `src/plugin.cpp` | Entry points of the Schwung Audio FX API v2, saved state |
| `scripts/param_spec.py` | Definition of every parameter and the page layout (`module.json`'s chain_params and the DSP's tables and ui_hierarchy are generated from it) |
| `scripts/gen_tables.py` | Resampler coefficients (designed so the round trip is a whole number of samples) |
| `scripts/fetch_warps.sh` | The pinned source commit and verification of the import |
| `tests/` | The tests above |

## Licence

This module's code is MIT (`LICENSE`). The imported Mutable Instruments /
Parasites / stmlib code is MIT too. Details in `THIRD_PARTY_LICENSES.md`.

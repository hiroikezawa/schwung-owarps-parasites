"""The one list of oWarps Parasites parameters.

scripts/metadata.py turns this into module.json's chain_params, the C table
the DSP uses (build/generated/owarps_params.h) and the ui_hierarchy the DSP
serves, so the three can never disagree.

Every Warps control is stored as what it is on the module, a POT POSITION
(0..1, shown as %), except where a signed range reads better: Doppler X,
frequency shift and tape speed are -1..+1 with 0 at the pot's noon. Pitch is
a MIDI note number (the firmware's 36..96 range for the LEVEL 1 pot).

Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
"""

MODES = [
    # (enum name, key prefix, page label)
    ("DOPPLER", "dop", "Doppler"),
    ("WAVEFOLDER", "fold", "Wavefolder"),
    ("CHEBYSCHEV", "cheb", "Chebyschev"),
    ("FREQ SHIFT", "fs", "Freq Shifter"),
    ("BIT MANGLER", "bit", "Bit Mangler"),
    ("COMPARATOR", "cmp", "Comparator"),
    ("VOCODER", "voc", "Vocoder"),
    ("DELAY", "dl", "Delay"),
    ("META", "meta", "Meta"),
]

OSC_XMOD = ["EXT", "SINE", "TRIANGLE", "SAW"]          # Oscillator shapes 0..2
OSC_VOCODER = ["EXT", "SAW", "PULSE", "NOISE"]         # carrier_shape + 1
OSC_META = ["EXT", "SIN/SAW", "TRI/PLS", "SAW/NSE"]    # xmod / vocoder shape
FS_CARRIER = ["IN 1", "SINE", "HARMONIC", "BUZZY"]     # quadrature wavetables
ROOM = ["TINY", "SMALL", "LARGE", "HUGE"]
TOPOLOGY = ["OPEN LOOP", "DUAL", "ANALOG", "PING-PONG"]


def pct(key, name, short, default, lo=0.0, hi=1.0):
    return dict(key=key, name=name, short_name=short, type="float", min=lo, max=hi,
                step=0.01, default=default, unit="%", display_format="%.0f")


def enum(key, name, short, options, default):
    return dict(key=key, name=name, short_name=short, type="enum", options=options,
                default=options[default])


def pitch(key, default=48):
    return dict(key=key, name="Osc Pitch", short_name="PITCH", type="int", min=36, max=96,
                step=1, default=default, unit="st")


# Per mode, in knob order 2..6 (+ PITCH, which replaces LEVEL 1 on the page
# while the internal oscillator is on - on the module they are one pot).
# Each entry: (param, warps control it stands in for)
PER_MODE = {
    "dop": [
        (pct("dop_x", "X Position", "X", 0.0, -1.0, 1.0), "algo"),
        (pct("dop_y", "Y Position", "Y", 0.25), "param"),
        (pct("dop_rate", "LFO Rate", "LFO RT", 0.3), "level1"),
        (pct("dop_amp", "LFO Amount", "LFO AM", 0.4), "level2"),
        (enum("dop_room", "Room Size", "ROOM", ROOM, 1), "button"),
    ],
    "fold": [
        (pct("fold_amt", "Fold", "FOLD", 0.5), "algo"),
        (pct("fold_bias", "Bias", "BIAS", 0.0), "param"),
        (pct("fold_l1", "Level 1", "LEVEL1", 0.7), "level1"),
        (pct("fold_l2", "Level 2", "LEVEL2", 0.7), "level2"),
        (enum("fold_osc", "Oscillator", "OSC", OSC_XMOD, 0), "button"),
        (pitch("fold_pitch"), "pitch"),
    ],
    "cheb": [
        (pct("cheb_order", "Order", "ORDER", 0.3), "algo"),
        (pct("cheb_gain", "Gain", "GAIN", 0.5), "param"),
        (pct("cheb_l1", "Level 1", "LEVEL1", 0.7), "level1"),
        (pct("cheb_l2", "Level 2", "LEVEL2", 0.7), "level2"),
        (enum("cheb_osc", "Oscillator", "OSC", OSC_XMOD, 0), "button"),
        (pitch("cheb_pitch"), "pitch"),
    ],
    "fs": [
        (pct("fs_shift", "Shift", "SHIFT", 0.25, -1.0, 1.0), "algo"),
        (pct("fs_updn", "Up/Down", "UP/DN", 0.0), "param"),
        (pct("fs_fdbk", "Feedback", "FDBK", 0.0), "level1"),
        (pct("fs_wet", "Dry/Wet", "DRY/WT", 1.0), "level2"),
        (enum("fs_car", "Carrier", "CARR", FS_CARRIER, 1), "button"),
    ],
    "bit": [
        (pct("bit_degr", "Degrade", "DEGRD", 0.3), "algo"),
        (pct("bit_morph", "Cross Mod", "XMOD", 0.5), "param"),
        (pct("bit_l1", "Level 1", "LEVEL1", 0.7), "level1"),
        (pct("bit_l2", "Level 2", "LEVEL2", 0.7), "level2"),
        (enum("bit_osc", "Oscillator", "OSC", OSC_XMOD, 0), "button"),
        (pitch("bit_pitch"), "pitch"),
    ],
    "cmp": [
        (pct("cmp_func", "Compare", "COMPAR", 0.3), "algo"),
        (pct("cmp_order", "Order", "ORDER", 0.3), "param"),
        (pct("cmp_l1", "Level 1", "LEVEL1", 0.7), "level1"),
        (pct("cmp_l2", "Level 2", "LEVEL2", 0.7), "level2"),
        (enum("cmp_osc", "Oscillator", "OSC", OSC_XMOD, 0), "button"),
        (pitch("cmp_pitch"), "pitch"),
    ],
    "voc": [
        (pct("voc_warp", "Warp", "WARP", 0.5), "algo"),
        (pct("voc_rel", "Release", "RELEAS", 0.3), "param"),
        (pct("voc_l1", "Carrier Level", "LEVEL1", 0.7), "level1"),
        (pct("voc_l2", "Modulator Level", "LEVEL2", 0.7), "level2"),
        (enum("voc_osc", "Carrier Osc", "OSC", OSC_VOCODER, 1), "button"),
        (pitch("voc_pitch"), "pitch"),
    ],
    "dl": [
        (pct("dl_speed", "Tape Speed", "SPEED", 1.0, -1.0, 1.0), "algo"),
        (pct("dl_dist", "Head Distance", "DIST", 0.5), "param"),
        (pct("dl_fdbk", "Feedback", "FDBK", 0.4), "level1"),
        (pct("dl_wet", "Dry/Wet", "DRY/WT", 0.5), "level2"),
        (enum("dl_topo", "Topology", "TOPO", TOPOLOGY, 1), "button"),
    ],
    "meta": [
        (pct("meta_algo", "Algorithm", "ALGO", 0.25), "algo"),
        (pct("meta_timb", "Timbre", "TIMBRE", 0.5), "param"),
        (pct("meta_l1", "Level 1", "LEVEL1", 0.7), "level1"),
        (pct("meta_l2", "Level 2", "LEVEL2", 0.7), "level2"),
        (enum("meta_osc", "Oscillator", "OSC", OSC_META, 0), "button"),
        (pitch("meta_pitch"), "pitch"),
    ],
}

GLOBAL = [
    pct("mix", "Mix", "MIX", 1.0),
    enum("mode", "Mode", "MODE", [m[0] for m in MODES], 8),
    dict(key="out_level", name="Output Level", short_name="OUT", type="float", min=-24.0,
         max=12.0, step=0.5, default=0.0, unit="dB", display_format="%.1f"),
    enum("routing", "Input Routing", "ROUTE", ["DUAL", "MONO", "CARRIER", "PRESERVE"], 0),
    enum("outputs", "Outputs", "OUTPUT", ["MAIN+AUX", "MAIN ONLY"], 0),
    dict(key="in_gain", name="Input Gain", short_name="IN", type="float", min=-24.0,
         max=12.0, step=0.5, default=0.0, unit="dB", display_format="%.1f"),
    enum("osc_src", "Osc Pitch Source", "PITCH", ["KNOB", "MIDI"], 0),
    enum("loop_patch", "Open Loop Patch", "PATCH", ["AUX>IN2", "IN 2"], 0),
    enum("bypass", "Bypass", "BYPASS", ["OFF", "ON"], 0),
    # The module's outputs are AC-coupled (codec + coupling capacitors); some
    # algorithms put out DC (even Chebyschev orders, the bit mangler's OR).
    enum("dc_block", "DC Block", "DC CUT", ["ON", "OFF"], 0),
]


def all_params():
    out = list(GLOBAL)
    for _, prefix, _ in MODES:
        out += [p for p, _ in PER_MODE[prefix]]
    return out


def hierarchy():
    """One page per mode (only the active mode's page is visible), then the
    Settings page. Knob 1 is MIX and knob 8 is MODE on every mode page."""
    levels = {}
    root_params = []
    for name, prefix, label in MODES:
        entries = PER_MODE[prefix]
        params = ["mix"]
        by_role = {role: p for p, role in entries}
        osc = by_role["button"]["key"]
        for p, role in entries:
            if role == "button":
                continue
            if role == "level1" and "pitch" in by_role:
                params.append({"key": p["key"], "visible_if": {"param": osc, "equals": "EXT"}})
                params.append({"key": by_role["pitch"]["key"],
                               "visible_if": {"param": osc, "not_equals": "EXT"}})
            elif role == "pitch":
                continue
            else:
                params.append(p["key"])
        params += [osc, "out_level", "mode"]
        gate = {"param": "mode", "equals": name}
        levels[prefix] = {"label": label, "visible_if": gate, "params": params}
        root_params.append({"level": prefix, "label": label})
    root_params.append({"level": "settings", "label": "Settings"})
    levels["settings"] = {"label": "Settings",
                          "params": ["mode", "routing", "outputs", "in_gain", "osc_src",
                                     "loop_patch", "dc_block", "bypass", "mix"]}
    levels["root"] = {"label": "oWarps", "params": root_params}
    return {"modes": None, "levels": levels}

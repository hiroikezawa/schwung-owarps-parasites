/* oWarps Parasites UI test: Schwung's REAL page controller and input decoder
 * (from a Schwung checkout or a copy of the device's shared/), loading the
 * knob grid the way the shadow UI does - from the `ui_hierarchy` the built
 * DSP serves - against a fake param store holding the DSP's real answers
 * (tests/dump_contract.py), drawn through Schwung's framebuffer harness.
 *
 * visible_if is answered the way shadow_ui.js's evaluateVisibilityCondition
 * answers it on the grid (string equality against the current value).
 *
 *   OW_SHARED     a shared/ directory to take the page engine from (e.g. one
 *                 mirrored from the device); default <schwung>/src/shared
 *   SCHWUNG_ROOT  a Schwung checkout (for tools/param-pages/harness.mjs);
 *                 default ../schwung beside this repository
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE). */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = process.env.SCHWUNG_ROOT || path.resolve(here, "../../schwung");
const shared = process.env.OW_SHARED || path.join(root, "src/shared");
const load = (p) => import(pathToFileURL(p).href);
const { createController } = await load(path.join(shared, "param_pages/page_controller.mjs"));
const { decodeInput, applyInput } = await load(path.join(shared, "param_pages/page_input.mjs"));
const { createFramebuffer, drawContext } = await load(path.join(root, "tools/param-pages/harness.mjs"));

let failures = 0;
function check(cond, msg) {
    if (!cond) { failures++; console.error("FAIL:", msg); }
    else console.log("ok  ", msg.split(":")[0]);
}

const contract = JSON.parse(fs.readFileSync(path.join(here, "../build/native/contract.json"), "utf8"));
const manifest = JSON.parse(fs.readFileSync(path.join(here, "../module.json"), "utf8"));
const chainParams = JSON.parse(contract.chain_params);
const values = { ...contract.values };
const writes = [];
const ENUMS = {};
for (const p of chainParams) if (p.type === "enum") ENUMS[p.key] = p.options;
const bare = (k) => k.slice(k.indexOf(":") + 1);
const store = {
    get(full) {
        const key = bare(full);
        if (key === "ui_hierarchy") return contract.ui_hierarchy;
        if (key === "chain_params") return contract.chain_params;
        if (key in values) return values[key];
        return "";
    },
    set(full, value) {
        const key = bare(full);
        writes.push([key, String(value)]);
        if (!(key in values)) return true;
        /* as plugin.cpp: an enum takes its name or its index, and reports the name */
        if (ENUMS[key]) {
            const s = String(value);
            const i = ENUMS[key].findIndex((o) => o.toLowerCase() === s.toLowerCase());
            let n = i >= 0 ? i : Math.round(Number(s));
            if (Number.isFinite(n)) n = Math.max(0, Math.min(ENUMS[key].length - 1, n));
            if (ENUMS[key][n] !== undefined) values[key] = ENUMS[key][n];
        } else {
            values[key] = Number(value).toFixed(4);
        }
        return true;
    },
};
/* shadow_ui.js compareConditionValue: String(actual) === String(expected) */
const visible = (cond) => {
    if (!cond || typeof cond !== "object") return true;
    const raw = values[cond.param];
    if (raw === null || raw === undefined) return true;
    if (cond.equals !== undefined) return String(raw) === String(cond.equals);
    if (cond.not_equals !== undefined) return String(raw) !== String(cond.not_equals);
    return true;
};

let clock = 1000;
const fb = createFramebuffer();
const ctx = drawContext(fb);
const c = createController({ getParam: store.get, setParam: store.set, now: () => clock });
c.load({ slot: 0, component: "synth", prefix: "synth", visible });
c.setLayout && c.setLayout("movy");
function ticks(n) {
    for (let i = 0; i < n; i++) {
        clock += 17;
        c.tick();
        fb.clearScreen();
        c.render(ctx, { title: "oWarps" });
        c.renderOverlays(ctx, { clearScreen: () => fb.clearScreen() });
    }
}
const midi = (d) => { const it = decodeInput(d, {}); if (it) applyInput(c, it, { nowMs: clock }); };
const shot = (name) => {
    const dir = path.join(here, "../build/screens");
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, name + ".png"), fb.toPng(4));
};
const keys = () => c.page.keys.map(bare);
const knobKey = (cc) => {
    const w = writes.length;
    for (let i = 0; i < 3; i++) { midi([0xb0, cc, 1]); ticks(1); }
    ticks(10);
    const hit = writes.slice(w).find((x) => x[0] !== "mode" || cc === 78);
    for (let i = 0; i < 3; i++) { midi([0xb0, cc, 127]); ticks(1); }   // and back
    ticks(10);
    return hit ? hit[0] : null;
};

const MODE_PAGES = {
    DOPPLER: ["mix", "dop_x", "dop_y", "dop_rate", "dop_amp", "dop_room", "out_level", "mode"],
    WAVEFOLDER: ["mix", "fold_amt", "fold_bias", "fold_l1", "fold_l2", "fold_osc", "out_level", "mode"],
    CHEBYSCHEV: ["mix", "cheb_order", "cheb_gain", "cheb_l1", "cheb_l2", "cheb_osc", "out_level", "mode"],
    "FREQ SHIFT": ["mix", "fs_shift", "fs_updn", "fs_fdbk", "fs_wet", "fs_car", "out_level", "mode"],
    "BIT MANGLER": ["mix", "bit_degr", "bit_morph", "bit_l1", "bit_l2", "bit_osc", "out_level", "mode"],
    COMPARATOR: ["mix", "cmp_func", "cmp_order", "cmp_l1", "cmp_l2", "cmp_osc", "out_level", "mode"],
    VOCODER: ["mix", "voc_warp", "voc_rel", "voc_pitch", "voc_l2", "voc_osc", "out_level", "mode"],
    DELAY: ["mix", "dl_speed", "dl_dist", "dl_fdbk", "dl_wet", "dl_topo", "out_level", "mode"],
    META: ["mix", "meta_algo", "meta_timb", "meta_l1", "meta_l2", "meta_osc", "out_level", "mode"],
};

ticks(40);
check(!("ui_chain" in manifest), "no ui_chain.js: Schwung's own grid everywhere (slots, Master FX, buses)");
check(JSON.stringify(keys()) === JSON.stringify(MODE_PAGES.META),
      "opens on the META page (factory mode): " + keys());
check(c.pages.length === 2, "only the active mode's page + Settings: " + c.pages.map((p) => p.name));
check(fb.countLit() > 200, "page draws: " + fb.countLit());
shot("meta");

/* every mode page: knob 1 = MIX, knob 8 = MODE, knobs 2-7 the mode's own */
for (const [mode, want] of Object.entries(MODE_PAGES)) {
    values.mode = mode;
    c.reloadIfChanged ? c.reloadIfChanged({ visible }) : null;
    store.set("synth:mode", mode);
    c.load({ slot: 0, component: "synth", prefix: "synth", visible });
    ticks(30);
    check(JSON.stringify(keys()) === JSON.stringify(want), `${mode} page = ${want.join(" ")}: ` + keys());
    const k1 = knobKey(71);
    check(k1 === "mix", `${mode}: knob 1 writes MIX: ` + k1);
    shot("page-" + mode.toLowerCase().replace(/ /g, "-"));
}

/* turning MODE (knob 8) moves the page to the new mode */
values.mode = "META";
c.load({ slot: 0, component: "synth", prefix: "synth", visible });
ticks(30);
for (let i = 0; i < 4; i++) { midi([0xb0, 78, 127]); ticks(3); }
ticks(40);
check(values.mode !== "META" && JSON.stringify(keys()) === JSON.stringify(MODE_PAGES[values.mode]),
      "knob 8 turns MODE and the page follows: " + values.mode + " " + keys());

/* the oscillator on: LEVEL 1's knob becomes PITCH (one pot on the module) */
values.mode = "WAVEFOLDER";
c.load({ slot: 0, component: "synth", prefix: "synth", visible });
ticks(30);
for (let i = 0; i < 12 && values.fold_osc === "EXT"; i++) { midi([0xb0, 76, 1]); ticks(4); }
ticks(40);
check(values.fold_osc !== "EXT" && keys()[3] === "fold_pitch",
      "OSC on (knob 6) -> knob 4 is PITCH: " + values.fold_osc + " " + keys());
shot("wavefolder-osc");

/* Settings by jog */
midi([0xb0, 14, 1]);
ticks(20);
check(JSON.stringify(keys()) === JSON.stringify(["routing", "outputs", "in_gain", "osc_src",
      "loop_patch", "dc_block", "bypass"]), "jog -> Settings ROUTE OUTPUT IN PITCH PATCH DC BYPASS: " + keys());
shot("settings");

/* every key the grid can show is a chain param; every chain param is on the grid */
const all = chainParams.map((p) => p.key);
const h = JSON.parse(contract.ui_hierarchy);
const onGrid = new Set();
for (const lvl of Object.values(h.levels)) for (const e of lvl.params) {
    const k = typeof e === "string" ? e : e.key;
    if (k) onGrid.add(k);
}
check([...onGrid].every((k) => all.includes(k)), "every grid key is in chain_params");
check(all.every((k) => onGrid.has(k)), "every parameter is reachable on the grid: " +
      all.filter((k) => !onGrid.has(k)));
check(manifest.capabilities.chain_params.length === all.length, "module.json == DSP chain_params");

if (failures) { console.error(`${failures} UI FAILURE(S)`); process.exit(1); }
console.log("ALL UI TESTS PASSED");

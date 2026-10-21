#!/usr/bin/env python3
"""Write what the built owarps_parasites.so serves to the UI (ui_hierarchy,
chain_params and every parameter's value) to build/native/contract.json, so
tests/test_ui.mjs runs against the DSP's real answers rather than a copy.

Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
"""
import ctypes as c
import json
import pathlib
import sys

lib = c.CDLL(sys.argv[1])


class Host(c.Structure):
    _fields_ = [("api_version", c.c_uint32), ("sample_rate", c.c_int),
                ("frames_per_block", c.c_int), ("rest", c.c_void_p * 32)]


class API(c.Structure):
    _fields_ = [("version", c.c_uint32)] + [(k, c.c_void_p) for k in
               ("create", "destroy", "process", "set", "get", "midi")]


lib.move_audio_fx_init_v2.restype = c.POINTER(API)
api = lib.move_audio_fx_init_v2(c.byref(Host(1, 44100, 128))).contents
create = c.CFUNCTYPE(c.c_void_p, c.c_char_p, c.c_char_p)(api.create)
get = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_char_p, c.c_char_p, c.c_int)(api.get)
inst = create(b"", None)


def read(key):
    buf = c.create_string_buffer(131072)
    n = get(inst, key.encode(), buf, 131072)
    return None if n < 0 else buf.value.decode()


keys = [p["key"] for p in json.loads(read("chain_params"))]
out = {"ui_hierarchy": read("ui_hierarchy"),
       "chain_params": read("chain_params"), "values": {k: read(k) for k in keys}}
json.loads(out["ui_hierarchy"])
pathlib.Path(sys.argv[2]).write_text(json.dumps(out, indent=1))
print("contract dumped:", sys.argv[2])

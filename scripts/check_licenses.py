#!/usr/bin/env python3
"""Fail if anything copyleft could have crept into the source or the package.

  * every vendored Mutable Instruments / Parasites / stmlib file keeps its
    copyright line and the full MIT permission notice;
  * no GPL / LGPL / AGPL text or SPDX tag anywhere in what is compiled or
    shipped (the licence documents themselves excepted, which name them only
    to say they are NOT used);
  * the package carries LICENSE and THIRD_PARTY_LICENSES.md.

Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
"""
import pathlib
import re
import sys
import tarfile

root = pathlib.Path(__file__).resolve().parent.parent
errors = []

for f in sorted((root / "src" / "mi").rglob("*")):
    if not f.is_file() or f.name == "LICENSE":
        continue
    text = f.read_text(errors="replace")
    if not re.search(r"Copyright 20\d\d (Emilie Gillet|Olivier Gillet|Matthias Puech)", text):
        errors.append(f"{f.relative_to(root)}: Emilie/Olivier Gillet copyright line missing")
    if "Permission is hereby granted, free of charge" not in text:
        errors.append(f"{f.relative_to(root)}: MIT permission notice missing")

copyleft = re.compile(r"GNU (Lesser |Affero )?General Public|SPDX-License-Identifier:\s*\S*GPL|\bL?A?GPL-?[23]")
exempt = {"LICENSE", "THIRD_PARTY_LICENSES.md", "README.md", "check_licenses.py"}
for f in sorted(root.rglob("*")):
    rel = f.relative_to(root)
    if not f.is_file() or rel.parts[0] in ("build", "dist", ".git") or f.name in exempt:
        continue
    if f.suffix not in (".cc", ".cpp", ".h", ".c", ".mjs", ".js", ".json", ".py", ".sh", ".patch"):
        continue
    if copyleft.search(f.read_text(errors="replace")):
        errors.append(f"{rel}: copyleft licence text")

pkg = root / "dist" / "owarps_parasites-module.tar.gz"
if pkg.exists():
    with tarfile.open(pkg) as t:
        names = set(t.getnames())
    for need in ("owarps_parasites/LICENSE", "owarps_parasites/THIRD_PARTY_LICENSES.md",
                 "owarps_parasites/owarps_parasites.so"):
        if need not in names:
            errors.append(f"package lacks {need}")

if errors:
    print("\n".join("FAIL: " + e for e in errors))
    sys.exit(1)
n = sum(1 for f in (root / "src" / "mi").rglob("*") if f.is_file() and f.name != "LICENSE")
print(f"PASS: {n} vendored files carry their MIT notice (Emilie / Olivier Gillet); no copyleft text; "
      f"package notices {'present' if pkg.exists() else 'not built yet'}")

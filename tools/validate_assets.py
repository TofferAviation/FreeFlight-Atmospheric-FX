#!/usr/bin/env python3
"""Small structural validator for FFAtmo OBJ/PSS authoring errors."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
PSS = ROOT / "assets" / "FFAtmo_particles.pss"
OBJ = ROOT / "assets" / "FFAtmo_Lineage.obj"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


pss = PSS.read_text(encoding="utf-8")
obj = OBJ.read_text(encoding="utf-8")

for line_number, line in enumerate(pss.splitlines(), start=1):
    if line.startswith("#"):
        fail(f"unsupported PSS comment at line {line_number}")

if not pss.startswith("A\n1000\nPARTICLE_SYSTEM"):
    fail("unexpected PSS header")
if pss.count("\nPARTICLE\n") != pss.count("\nEND_PARTICLE\n"):
    fail("unbalanced particle blocks")
if pss.count("\nEMITTER\n") != pss.count("\nEND_EMITTER"):
    fail("unbalanced emitter blocks")
if pss.count("\nSUB_EMITTER\n") != pss.count("\nEND_SUB_EMITTER\n"):
    fail("unbalanced sub-emitter blocks")

particle_count = pss.count("\nPARTICLE\n")
for value in re.findall(r"MAX_PARTICLES\s+(\d+)", pss):
    if int(value) > 16384:
        fail(f"MAX_PARTICLES {value} exceeds X-Plane's per-type limit")
for value in re.findall(r"PARTICLE_TYPE\s+(\d+)", pss):
    if int(value) >= particle_count:
        fail(f"particle type {value} is outside 0..{particle_count - 1}")

for block in re.findall(r"\nEMITTER\n(.*?)\nEND_EMITTER", pss, flags=re.DOTALL):
    count_match = re.search(r"\nDATAREFS\s+(\d+)\n", block)
    if not count_match:
        fail("emitter has no DATAREFS declaration")
    declared = int(count_match.group(1))
    drefs = re.findall(r"^DREF\s+.+$", block, flags=re.MULTILINE)
    if len(drefs) != declared:
        fail(f"emitter declares {declared} datarefs but contains {len(drefs)}")
    slots = [int(value) for value in re.findall(r"^SLOT\s+(\d+)$", block, flags=re.MULTILINE)]
    if slots and max(slots) >= declared:
        fail(f"emitter uses slot {max(slots)} with only {declared} datarefs")

emitter_names = set(re.findall(r"\n NAME (FFATMO_[A-Z_]+)\n", pss))
attached_names = re.findall(r"^EMITTER\s+(FFATMO_[A-Z_]+)\s", obj, flags=re.MULTILINE)
for name in attached_names:
    if name not in emitter_names:
        fail(f"OBJ references missing emitter {name}")

texture_match = re.search(r"^TEXTURE\s+(.+)$", obj, flags=re.MULTILINE)
particle_match = re.search(r"^PARTICLE_SYSTEM\s+(.+)$", obj, flags=re.MULTILINE)
if not texture_match or not (ROOT / "assets" / texture_match.group(1)).exists():
    fail("OBJ texture is missing")
if not particle_match or not (ROOT / "assets" / particle_match.group(1)).exists():
    fail("OBJ particle system is missing")

print(f"FFAtmo assets passed: {particle_count} particles, "
      f"{len(emitter_names)} emitter types, {len(attached_names)} attachments")

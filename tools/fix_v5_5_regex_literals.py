#!/usr/bin/env python3
"""Prevent re.sub from interpreting C++ backslash escapes in the v5.5 patcher."""
from pathlib import Path

path = Path("tools/patch_v5_5_parcel_clusters.py")
text = path.read_text(encoding="utf-8")

old_update = '''    update_replacement + "\\n    void setEnabled",'''
new_update = '''    lambda _match: update_replacement + "\\n    void setEnabled",'''
old_billboard = '''    set_billboard_replacement + "\\n    void hideInstance",'''
new_billboard = '''    lambda _match: set_billboard_replacement + "\\n    void hideInstance",'''

if old_update not in text and new_update not in text:
    raise RuntimeError("v5.5 update replacement call was not found")
if old_billboard not in text and new_billboard not in text:
    raise RuntimeError("v5.5 billboard replacement call was not found")

text = text.replace(old_update, new_update, 1)
text = text.replace(old_billboard, new_billboard, 1)
path.write_text(text, encoding="utf-8", newline="\n")
print("Restored literal callback replacements in the v5.5 patcher")

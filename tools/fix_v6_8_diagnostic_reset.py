#!/usr/bin/env python3
from pathlib import Path
import runpy

path = Path(__file__).with_name("patch_v6_8_visible_primary_rollup.py")
text = path.read_text(encoding="utf-8")
old = '''# Reset visible roll-up diagnostics each frame.
r = once(
    r,
    "        maximumWakeDescentM_ = 0.0;\\n        swirlCandidateCount_ = 0;",
    "        maximumWakeDescentM_ = 0.0;\\n        maximumPrimaryRollupOffsetM_ = 0.0;\\n        primaryRollupCloudletCount_ = 0;\\n        swirlCandidateCount_ = 0;",
    "v6.8 rollup diagnostic reset",
)
'''
new = '''# Reset visible roll-up diagnostics at startup and every frame. The baseline has
# the same reset pair in both locations, so update both intentionally.
reset_old = "        maximumWakeDescentM_ = 0.0;\\n        swirlCandidateCount_ = 0;"
reset_new = "        maximumWakeDescentM_ = 0.0;\\n        maximumPrimaryRollupOffsetM_ = 0.0;\\n        primaryRollupCloudletCount_ = 0;\\n        swirlCandidateCount_ = 0;"
reset_count = r.count(reset_old)
if reset_count != 2:
    raise RuntimeError(f"v6.8 rollup diagnostic reset: expected two matches, found {reset_count}")
r = r.replace(reset_old, reset_new)
'''
if old not in text:
    raise RuntimeError("v6.8 diagnostic reset patch block not found")
text = text.replace(old, new, 1)
path.write_text(text, encoding="utf-8", newline="\n")
print("Stabilized v6.8 diagnostic reset matcher")

# The established v6.8 retry workflow calls this stabilizer before the main
# v6.8 patcher. Apply the curved-fill matcher repair here as well so the same
# trusted workflow exercises both corrections without requiring another sim build.
runpy.run_path(str(Path(__file__).with_name("fix_v6_8_fill_matcher.py")), run_name="__main__")

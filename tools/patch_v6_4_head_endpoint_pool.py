#!/usr/bin/env python3
"""Integrate v6.4 explicit near-field endpoint and less-fragmented instance pools."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLANNER = ROOT / "src" / "render" / "ContrailRenderPlanner.cpp"
RENDERER = ROOT / "src" / "ContrailCloudletRendererV62.h"
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


# ---------------------------------------------------------------------------
# Planner: v6.3 positioned the synthetic head at ~18 m but never emitted that
# endpoint. Segment sampling uses midpoint locations only, which is why the
# report still measured ~70 m. Emit one actual synthetic-head candidate before
# subdividing the head->physical segment.
# ---------------------------------------------------------------------------
p = PLANNER.read_text(encoding="utf-8")
head_anchor = '''        if (stream.size() == 1) {
            const float width = compositeHalfWidthForAge(
                stream.front().ageSeconds, stream.front().physicsRadiusM) * 2.0f;'''
head_block = '''        const auto syntheticHeadIt = std::find_if(
            stream.begin(), stream.end(), [](const auto& item) { return item.syntheticHead; });
        if (syntheticHeadIt != stream.end()) {
            const auto& head = *syntheticHeadIt;
            engine::Vec3d tangent {0.0, 0.0, -1.0};
            if (stream.size() > 1) {
                const auto headIndex = static_cast<std::size_t>(
                    std::distance(stream.begin(), syntheticHeadIt));
                const auto neighbourIndex = headIndex > 0 ? headIndex - 1u : 1u;
                tangent = normalized(
                    subtract(head.localPositionM, stream[neighbourIndex].localPositionM),
                    {0.0, 0.0, -1.0});
            }
            const engine::Vec3d right = normalized(
                cross(tangent, {0.0, 1.0, 0.0}), {1.0, 0.0, 0.0});
            const engine::Vec3d up = normalized(
                cross(right, tangent), {0.0, 1.0, 0.0});
            const float width = compositeHalfWidthForAge(
                head.ageSeconds, head.physicsRadiusM) * 2.0f;
            appendComposite(
                candidates,
                statistics,
                head,
                head,
                head.localPositionM,
                tangent,
                right,
                up,
                0.0f,
                std::max(width, 0.6f),
                0xfffffff0u,
                settings);
        }

''' + head_anchor
p = once(p, head_anchor, head_block, "explicit synthetic head endpoint")
if "syntheticHeadIt" not in p or "0xfffffff0u" not in p:
    raise RuntimeError("v6.4 explicit head endpoint was not integrated")
PLANNER.write_text(p, encoding="utf-8", newline="\n")


# ---------------------------------------------------------------------------
# Renderer: the equal 8x128 split left 224 of 1024 slots idle in the v6.3 run
# (800 visible). Keep eight morphology assets, but allocate the fixed 1024
# instances where this real run showed demand while retaining reserve at both
# extremes.
# ---------------------------------------------------------------------------
r = RENDERER.read_text(encoding="utf-8")
r = once(r, "static constexpr std::size_t kInstancesPerAsset = 128;",
         "static constexpr std::size_t kInstancesPerAsset = 164;", "storage pool maximum")
r = once(r, "static constexpr std::size_t kVisibleCapacity = kAssetCount * kInstancesPerAsset;",
         '''static constexpr std::size_t kVisibleCapacity = 1024;
    inline static constexpr std::array<std::size_t, kAssetCount> kAssetInstanceCapacities {
        48, 164, 164, 164, 164, 144, 144, 32
    };''', "adaptive pool capacities")

r = once(
    r,
    '''            if (!objects_[asset] || createdInstanceCount_[asset] >= kInstancesPerAsset) continue;
            const std::size_t target = std::min(
                createdInstanceCount_[asset] + kInstanceCreationBatchPerAsset,
                kInstancesPerAsset);''',
    '''            const std::size_t assetCapacity = kAssetInstanceCapacities[asset];
            if (!objects_[asset] || createdInstanceCount_[asset] >= assetCapacity) continue;
            const std::size_t target = std::min(
                createdInstanceCount_[asset] + kInstanceCreationBatchPerAsset,
                assetCapacity);''',
    "capacity-aware pool growth",
)
r = once(
    r,
    '''            if (createdInstanceCount_[asset] == kInstancesPerAsset && !poolReadyLogged_[asset]) {
                poolReadyLogged_[asset] = true;
                log("Renderer v6.3 asset " + std::to_string(asset) + " reached 128 persistent 3-D instances.\\n");
            }''',
    '''            if (createdInstanceCount_[asset] == assetCapacity && !poolReadyLogged_[asset]) {
                poolReadyLogged_[asset] = true;
                log("Renderer v6.4 asset " + std::to_string(asset) + " reached " +
                    std::to_string(assetCapacity) + " persistent 3-D instances.\\n");
            }''',
    "capacity-aware pool ready log",
)

r = r.replace("Renderer Foundation v6.3", "Renderer Foundation v6.4")
r = r.replace("Renderer v6.3", "Renderer v6.4")
r = r.replace("eight-asset soft/dense native 3-D morphology field",
              "continuous-shell native 3-D morphology field")
r = r.replace("Eight soft/dense irregular 3-D OBJ morphology assets are",
              "Eight porous single-surface 3-D OBJ morphology assets are")

for token in (
    "kVisibleCapacity = 1024",
    "kAssetInstanceCapacities",
    "48, 164, 164, 164, 164, 144, 144, 32",
    "assetCapacity = kAssetInstanceCapacities[asset]",
):
    if token not in r:
        raise RuntimeError(f"v6.4 renderer pool integration missing: {token}")
RENDERER.write_text(r, encoding="utf-8", newline="\n")


# ---------------------------------------------------------------------------
# Runtime identity/report capacity. The storage maximum is 164, but only the
# profile above is allocated; report the true fixed total rather than 8*164.
# ---------------------------------------------------------------------------
q = PLUGIN.read_text(encoding="utf-8")
q = once(
    q,
    "ContrailCloudletRenderer::kAssetCount * ContrailCloudletRenderer::kInstancesPerAsset",
    "ContrailCloudletRenderer::kVisibleCapacity",
    "pooled instance diagnostic",
)
q = q.replace(
    "FFAtmo World Contrail Visual Debug Report v6.3 NATIVE_3D_MORPHOLOGY_FIELD",
    "FFAtmo World Contrail Visual Debug Report v6.4 NATIVE_3D_CONTINUOUS_SHELL",
)
q = q.replace("3D CLOUD V6.3 READY", "3D CLOUD V6.4 READY")
q = q.replace("LOADING V6.3 MORPHOLOGY FIELD", "LOADING V6.4 CONTINUOUS SHELL FIELD")
q = q.replace("v6 point 3", "v6 point 4")
q = q.replace("v6.3", "v6.4")
q = q.replace("V6.3", "V6.4")
q = q.replace(
    "renders a soft/dense native 3-D morphology field with bounded near-field onset.",
    "renders porous continuous-shell 3-D cloudlets with an explicit nozzle-relative head endpoint.",
)
q = q.replace("native 3-D morphology field renderer.", "native continuous-shell 3-D field renderer.")

for token in (
    "NATIVE_3D_CONTINUOUS_SHELL",
    "3D CLOUD V6.4 READY",
    "ContrailCloudletRenderer::kVisibleCapacity",
):
    if token not in q:
        raise RuntimeError(f"v6.4 runtime integration missing: {token}")
PLUGIN.write_text(q, encoding="utf-8", newline="\n")

print("Integrated Renderer Foundation v6.4 explicit head endpoint + adaptive continuous-shell pool")

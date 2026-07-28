from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

LANE_NAMES = {0: "inner", 1: "core", 2: "outer", 3: "secondary"}


def vadd(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vmul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def mag(a):
    return math.sqrt(max(dot(a, a), 0.0))


def norm(a, fallback=(1.0, 0.0, 0.0)):
    length = mag(a)
    if not math.isfinite(length) or length < 1.0e-9:
        return fallback
    return vmul(a, 1.0 / length)


def distance(a, b):
    return mag(vsub(a, b))


def load_points(path: Path) -> List[dict]:
    points = []
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            points.append(
                {
                    "render_id": int(row["render_id"]),
                    "parcel_id": int(row["parcel_id"]),
                    "engine": int(row["engine"]),
                    "lane": int(row["lane"]),
                    "age": float(row["age_s"]),
                    "p": (float(row["x_m"]), float(row["y_m"]), float(row["z_m"])),
                    "t": (float(row["tx"]), float(row["ty"]), float(row["tz"])),
                }
            )
    if not points:
        raise RuntimeError("v6.9 marker field CSV is empty")
    return points


def unwrap(values: Iterable[float]) -> List[float]:
    out: List[float] = []
    for value in values:
        if not out:
            out.append(value)
            continue
        adjusted = value
        while adjusted - out[-1] > math.pi:
            adjusted -= 2.0 * math.pi
        while adjusted - out[-1] < -math.pi:
            adjusted += 2.0 * math.pi
        out.append(adjusted)
    return out


def frame(points: List[dict]):
    core = [p for p in points if p["lane"] == 1]
    tangent_sum = (0.0, 0.0, 0.0)
    for point in core or points:
        tangent_sum = vadd(tangent_sum, point["t"])
    longitudinal = norm(tangent_sum)

    world_up = (0.0, 1.0, 0.0)
    lateral = norm(cross(longitudinal, world_up), (0.0, 0.0, 1.0))
    vertical = norm(cross(lateral, longitudinal), world_up)

    origin = (0.0, 0.0, 0.0)
    for point in points:
        origin = vadd(origin, point["p"])
    origin = vmul(origin, 1.0 / len(points))
    return origin, longitudinal, lateral, vertical


def project(point, origin, longitudinal, lateral, vertical):
    relative = vsub(point, origin)
    return (
        dot(relative, longitudinal),
        dot(relative, lateral),
        dot(relative, vertical),
    )


def metrics(points: List[dict]) -> dict:
    origin, longitudinal, lateral_axis, vertical_axis = frame(points)
    projected = []
    for point in points:
        long_m, lat_m, vert_m = project(
            point["p"], origin, longitudinal, lateral_axis, vertical_axis
        )
        copy = dict(point)
        copy.update(long_m=long_m, lat_m=lat_m, vert_m=vert_m)
        projected.append(copy)

    by_lane: Dict[Tuple[int, int], List[dict]] = defaultdict(list)
    by_section: Dict[Tuple[int, int], Dict[int, dict]] = defaultdict(dict)
    for point in projected:
        by_lane[(point["engine"], point["lane"])].append(point)
        by_section[(point["engine"], point["parcel_id"])][point["lane"]] = point

    maximum_lane_gap = 0.0
    maximum_curvature = 0.0
    curvature_samples = []
    for lane_points in by_lane.values():
        lane_points.sort(key=lambda p: (p["age"], p["parcel_id"]))
        for a, b in zip(lane_points, lane_points[1:]):
            maximum_lane_gap = max(maximum_lane_gap, distance(a["p"], b["p"]))
        for a, b, c in zip(lane_points, lane_points[1:], lane_points[2:]):
            first = norm(vsub(b["p"], a["p"]))
            second = norm(vsub(c["p"], b["p"]))
            angle = math.acos(max(-1.0, min(1.0, dot(first, second))))
            if math.isfinite(angle):
                curvature_samples.append(angle)
                maximum_curvature = max(maximum_curvature, angle)

    widths = []
    phase_by_engine: Dict[int, List[Tuple[float, float]]] = defaultdict(list)
    secondary_separations = []
    for (engine, _parcel), lanes in by_section.items():
        inner = lanes.get(0)
        outer = lanes.get(2)
        core = lanes.get(1)
        secondary = lanes.get(3)
        if inner and outer:
            widths.append(distance(inner["p"], outer["p"]))
            dlat = outer["lat_m"] - inner["lat_m"]
            dvert = outer["vert_m"] - inner["vert_m"]
            angle = math.atan2(dvert, dlat)
            phase_by_engine[engine].append((inner["age"], angle))
        if core and secondary:
            secondary_separations.append(distance(core["p"], secondary["p"]))

    phase_span_by_engine = {}
    for engine, values in phase_by_engine.items():
        values.sort(key=lambda item: item[0])
        angles = unwrap(angle for _age, angle in values)
        phase_span_by_engine[str(engine)] = angles[-1] - angles[0] if len(angles) >= 2 else 0.0

    latitudes = [p["lat_m"] for p in projected]
    verticals = [p["vert_m"] for p in projected]
    longitudinals = [p["long_m"] for p in projected]
    counts = defaultdict(int)
    for point in projected:
        counts[LANE_NAMES.get(point["lane"], str(point["lane"]))] += 1

    phase_values = list(phase_span_by_engine.values())
    counter_rotating = len(phase_values) >= 2 and phase_values[0] * phase_values[1] < 0.0

    result = {
        "render_point_count": len(projected),
        "lane_counts": dict(counts),
        "engine_count": len({p["engine"] for p in projected}),
        "longitudinal_extent_m": max(longitudinals) - min(longitudinals),
        "lateral_spread_m": max(latitudes) - min(latitudes),
        "vertical_spread_m": max(verticals) - min(verticals),
        "maximum_lane_gap_m": maximum_lane_gap,
        "maximum_lane_curvature_deg": math.degrees(maximum_curvature),
        "mean_lane_curvature_deg": math.degrees(
            sum(curvature_samples) / len(curvature_samples)
        ) if curvature_samples else 0.0,
        "mean_sheet_width_m": sum(widths) / len(widths) if widths else 0.0,
        "maximum_sheet_width_m": max(widths) if widths else 0.0,
        "mean_secondary_separation_m": (
            sum(secondary_separations) / len(secondary_separations)
            if secondary_separations else 0.0
        ),
        "phase_span_radians_by_engine": phase_span_by_engine,
        "counter_rotation_detected": counter_rotating,
        "frame": {
            "longitudinal": longitudinal,
            "lateral": lateral_axis,
            "vertical": vertical_axis,
        },
    }

    # Geometry-based gate. These deliberately inspect final solver-produced
    # marker positions rather than radius/phase formulas or generated candidates.
    failures = []
    for lane in ("inner", "core", "outer"):
        if result["lane_counts"].get(lane, 0) < 8:
            failures.append(f"too few rendered {lane} lane points")
    if result["lateral_spread_m"] < 3.0:
        failures.append("final rendered lateral spread remains rope-like")
    if result["vertical_spread_m"] < 1.0:
        failures.append("final rendered vertical spread remains rope-like")
    if result["maximum_sheet_width_m"] < 1.5:
        failures.append("wake cross-section failed to remain/develop broad material width")
    if result["maximum_lane_curvature_deg"] < 0.05:
        failures.append("rendered marker lanes are effectively straight")
    if result["maximum_lane_gap_m"] > 180.0:
        failures.append("rendered marker lane continuity gap is too large")
    if len(phase_values) >= 2:
        if not counter_rotating:
            failures.append("left/right wake phase does not counter-rotate")
        if min(abs(value) for value in phase_values[:2]) < 0.05:
            failures.append("rendered wake phase progression is too small")
    else:
        failures.append("insufficient engines for counter-rotation measurement")

    result["gate_passed"] = not failures
    result["gate_failures"] = failures
    return result, projected


def svg_escape(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def make_svg(points: List[dict], x_key: str, y_key: str, title: str, path: Path) -> None:
    width, height, pad = 1400, 800, 70
    xs = [p[x_key] for p in points]
    ys = [p[y_key] for p in points]
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    if abs(xmax - xmin) < 1.0e-6:
        xmax = xmin + 1.0
    if abs(ymax - ymin) < 1.0e-6:
        ymax = ymin + 1.0
    xmargin = 0.04 * (xmax - xmin)
    ymargin = 0.08 * (ymax - ymin)
    xmin -= xmargin
    xmax += xmargin
    ymin -= ymargin
    ymax += ymargin

    def sx(value):
        return pad + (value - xmin) / (xmax - xmin) * (width - 2 * pad)

    def sy(value):
        return height - pad - (value - ymin) / (ymax - ymin) * (height - 2 * pad)

    groups: Dict[Tuple[int, int], List[dict]] = defaultdict(list)
    for point in points:
        groups[(point["engine"], point["lane"])].append(point)

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#07101a"/>',
        f'<text x="{pad}" y="42" fill="white" font-family="Segoe UI,Arial" font-size="24">{svg_escape(title)}</text>',
        f'<rect x="{pad}" y="{pad}" width="{width-2*pad}" height="{height-2*pad}" fill="none" stroke="#35506a" stroke-width="1"/>',
    ]

    dash_by_lane = {0: "", 1: "", 2: "", 3: "8 7"}
    opacity_by_lane = {0: 0.75, 1: 1.0, 2: 0.75, 3: 0.50}
    # No semantic colour dependence: engine/lane identity is also represented by
    # continuous/dashed geometry and labels in metrics JSON.
    strokes = ["#f7fbff", "#9fd7ff"]
    for (engine, lane), values in sorted(groups.items()):
        values.sort(key=lambda p: (p["age"], p["parcel_id"]))
        coords = " ".join(f"{sx(p[x_key]):.1f},{sy(p[y_key]):.1f}" for p in values)
        if len(values) >= 2:
            dash = dash_by_lane.get(lane, "")
            dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
            parts.append(
                f'<polyline points="{coords}" fill="none" stroke="{strokes[engine % len(strokes)]}" '
                f'stroke-width="{2.6 if lane == 1 else 1.7}" opacity="{opacity_by_lane.get(lane,0.7)}"{dash_attr}/>'
            )
    parts.append('</svg>')
    path.write_text("\n".join(parts), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv_path", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--gate", action="store_true")
    args = parser.parse_args()

    points = load_points(args.csv_path)
    result, projected = metrics(points)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    make_svg(projected, "lat_m", "vert_m", "v6.9 actual WakeFluidSolver — rear view", args.output_dir / "v6_9_rear.svg")
    make_svg(projected, "long_m", "vert_m", "v6.9 actual WakeFluidSolver — side view", args.output_dir / "v6_9_side.svg")
    make_svg(projected, "long_m", "lat_m", "v6.9 actual WakeFluidSolver — top view", args.output_dir / "v6_9_top.svg")
    (args.output_dir / "v6_9_vortex_sheet_metrics.json").write_text(
        json.dumps(result, indent=2, sort_keys=True), encoding="utf-8"
    )

    print(json.dumps(result, indent=2, sort_keys=True))
    if args.gate and not result["gate_passed"]:
        raise SystemExit("v6.9 rendered marker field failed geometry gate: " + "; ".join(result["gate_failures"]))


if __name__ == "__main__":
    main()

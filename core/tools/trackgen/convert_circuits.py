"""Convert circuit centre lines (GeoJSON LineStrings) into UISim track data (M9.6).

Generic and public: real circuit data and names stay private (data/tracks/private, gitignored).

    python core/tools/trackgen/convert_circuits.py --geojson <file.geojson> --names <names.json>
        --template data/tracks/testring.json --out data/tracks/private

The line is projected to metres, smoothed (Catmull-Rom), scaled to the official length, resampled
every 5 m and cut into straights and corners by curvature. Corner speed comes from the tightest
radius, with grip growing with speed (downforce): v^2 = base * r / (1 - aero * r). Straight lengths are adjusted slightly so that the lap closes the way
the sim integrates it (constant-curvature arcs). Sectors, marshal sectors, track-limits corners and
Overtake mode lines are derived; tyres are copied from the template.
"""
import argparse
import json
import math
import os
import re

STEP = 5.0              # metres between samples
GRIP_BASE = 25.0        # m/s^2 lateral grip without downforce
GRIP_AERO = 0.0045      # downforce: extra lateral grip per (m/s)^2, so a = base + aero * v^2
OVERTAKE_BUDGET = 1800.0 # sum of length x overtake chance (testring: 2800; long straights pass more easily)
TOP_SPEED = 330.0       # km/h on straights
MIN_SPEED = 65.0        # km/h, tightest hairpin
STRAIGHT_RADIUS = 250.0 # metres: gentler curves count as straight
SMOOTH = 6              # samples either side for the curvature
POSITION_SMOOTH = 4     # samples either side when smoothing the line (removes zigzags in the source)
MIN_SEGMENT = 30.0      # metres
MAX_CORNER = 120.0      # metres: longer corners are split


def project(coords):
    lat0 = math.radians(sum(c[1] for c in coords) / len(coords))
    r = 6371000.0
    return [(r * math.radians(lon) * math.cos(lat0), r * math.radians(lat)) for lon, lat in coords]


def catmull_rom(points, sub=8):
    n = len(points)
    out = []
    for i in range(n):
        p0, p1, p2, p3 = points[i - 1], points[i], points[(i + 1) % n], points[(i + 2) % n]
        for k in range(sub):
            t = k / sub
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * ((2 * p1[d]) + (-p0[d] + p2[d]) * t + (2 * p0[d] - 5 * p1[d] + 4 * p2[d] - p3[d]) * t2 +
                                    (-p0[d] + 3 * p1[d] - 3 * p2[d] + p3[d]) * t3) for d in range(2)))
    return out


def loop_length(points):
    return sum(math.dist(points[i], points[(i + 1) % len(points)]) for i in range(len(points)))


def resample(points, step):
    n = len(points)
    total = loop_length(points)
    count = int(round(total / step))
    step = total / count
    out, i, seg_start = [], 0, 0.0
    seg_len = math.dist(points[0], points[1])
    for k in range(count):
        target = k * step
        while seg_start + seg_len < target and i < n:
            seg_start += seg_len
            i += 1
            seg_len = math.dist(points[i % n], points[(i + 1) % n])
        f = (target - seg_start) / seg_len if seg_len > 0 else 0.0
        a, b = points[i % n], points[(i + 1) % n]
        out.append((a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f))
    return out, step


def wrap_angle(a):
    return (a + math.pi) % (2 * math.pi) - math.pi


def integrate(segments):
    """End point of the lap the way core/src/Track.cpp builds it (midpoint, constant-curvature arcs)."""
    x = y = heading = 0.0
    starts = []
    for length, turn in segments:
        starts.append(heading)
        steps = max(1, math.ceil(length / 5.0))
        ds, dh = length / steps, turn / steps
        for _ in range(steps):
            heading += dh / 2
            x += ds * math.cos(heading)
            y += ds * math.sin(heading)
            heading += dh / 2
    return x, y, starts


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def convert(feature, name, template_tyres):
    props = feature["properties"]
    coords = feature["geometry"]["coordinates"]
    points = project(coords)
    if math.dist(points[0], points[-1]) < 1.0:
        points = points[:-1]
    smooth = catmull_rom(points)
    scale = props.get("length", loop_length(smooth)) / loop_length(smooth)
    smooth = [(p[0] * scale, p[1] * scale) for p in smooth]
    samples, step = resample(smooth, STEP)
    n = len(samples)
    for _ in range(2):  # moving average over about 40 m, twice: source points are 30-50 m apart and noisy
        samples = [(sum(samples[(i + k) % n][0] for k in range(-POSITION_SMOOTH, POSITION_SMOOTH + 1)) / (2 * POSITION_SMOOTH + 1),
                    sum(samples[(i + k) % n][1] for k in range(-POSITION_SMOOTH, POSITION_SMOOTH + 1)) / (2 * POSITION_SMOOTH + 1))
                   for i in range(n)]

    # Heading change per sample interval and smoothed curvature.
    heading = [math.atan2(samples[(i + 1) % n][1] - samples[i][1], samples[(i + 1) % n][0] - samples[i][0]) for i in range(n)]
    dtheta = [wrap_angle(heading[(i + 1) % n] - heading[i]) for i in range(n)]
    curvature = [sum(dtheta[(i + k) % n] for k in range(-SMOOTH, SMOOTH + 1)) / ((2 * SMOOTH + 1) * step) for i in range(n)]
    kind = [0 if abs(c) < 1.0 / STRAIGHT_RADIUS else (1 if c > 0 else -1) for c in curvature]

    # Runs of the same kind, starting at sample 0 (the start/finish line of the source data).
    runs = []
    for i in range(n):
        if runs and runs[-1][0] == kind[i]:
            runs[-1][2] += 1
        else:
            runs.append([kind[i], i, 1])
    changed = True
    while changed:  # merge runs that are too short into their predecessor
        changed = False
        for r in range(len(runs)):
            if runs[r][2] * step < MIN_SEGMENT and len(runs) > 1:
                prev = runs[r - 1] if r > 0 else runs[1]
                prev[2] += runs[r][2]
                if r == 0:
                    prev[1] = runs[r][1]
                del runs[r]
                changed = True
                break
        merged = []
        for run in runs:  # neighbours of the same kind become one run
            if merged and merged[-1][0] == run[0]:
                merged[-1][2] += run[2]
            else:
                merged.append(run)
        runs = merged
    pieces = []
    for kind_, start, count in runs:  # long corners in several pieces
        parts = max(1, math.ceil(count * step / MAX_CORNER)) if kind_ != 0 else 1
        for p in range(parts):
            a = start + count * p // parts
            b = start + count * (p + 1) // parts
            pieces.append((kind_, a, b))

    segments = []
    for kind_, a, b in pieces:
        turn = sum(dtheta[i % n] for i in range(a, b))
        bends = sorted(abs(curvature[i % n]) for i in range(a, b))
        tightest = bends[min(len(bends) - 1, int(0.9 * len(bends)))]  # robust against single spikes
        radius = 1.0 / max(tightest, 1e-6)
        denominator = 1.0 - GRIP_AERO * radius
        apex = math.sqrt(GRIP_BASE * radius / denominator) * 3.6 if denominator > 0.05 else TOP_SPEED
        speed = min(TOP_SPEED, max(MIN_SPEED, apex))
        segments.append({"kind": kind_, "length": (b - a) * step, "turn": turn, "speed": speed})

    # Close the lap the way the sim integrates it: least-squares change of the straight lengths.
    before = math.hypot(*integrate([(s["length"], s["turn"]) for s in segments])[:2])
    for _ in range(3):
        ex, ey, starts = integrate([(s["length"], s["turn"]) for s in segments])
        straights = [i for i, s in enumerate(segments) if s["kind"] == 0]
        dirs = [(math.cos(starts[i] + segments[i]["turn"] / 2), math.sin(starts[i] + segments[i]["turn"] / 2)) for i in straights]
        a11 = sum(d[0] * d[0] for d in dirs)
        a12 = sum(d[0] * d[1] for d in dirs)
        a22 = sum(d[1] * d[1] for d in dirs)
        det = a11 * a22 - a12 * a12
        if abs(det) < 1e-9:
            break
        lx = (-ex * a22 + ey * a12) / det
        ly = (ex * a12 - ey * a11) / det
        for i, d in zip(straights, dirs):
            segments[i]["length"] = max(segments[i]["length"] * 0.5, segments[i]["length"] + d[0] * lx + d[1] * ly)
    after = math.hypot(*integrate([(s["length"], s["turn"]) for s in segments])[:2])

    total = sum(s["length"] for s in segments)
    position = 0.0
    corner_no = straight_no = 0
    for s in segments:
        s["start"] = position
        position += s["length"]
        s["sector"] = 1 if s["start"] < total / 3 else (2 if s["start"] < 2 * total / 3 else 3)
        if s["kind"] == 0:
            straight_no += 1
            s["name"] = "Straight %d" % straight_no
            # A long straight lets almost every attack finish: count it like 700 m at most.
            s["overtake"] = round(max(0.1, min(1.0, s["length"] / 900.0) * min(1.0, 700.0 / s["length"])), 2)
        else:
            corner_no += 1
            s["name"] = "T%d" % corner_no
            s["overtake"] = 0.3 if s["speed"] < 140 else (0.1 if s["speed"] < 200 else 0.0)
    budget = sum(s["length"] * s["overtake"] for s in segments)
    for s in segments:  # the same overall chance as the testring, whatever the number of straights
        s["overtake"] = round(min(1.0, s["overtake"] * OVERTAKE_BUDGET / budget), 2)
    corners = sorted((s for s in segments if s["kind"] != 0 and 150 <= s["speed"] <= 280), key=lambda s: -s["speed"])
    if len(corners) < 2:
        corners = sorted((s for s in segments if s["kind"] != 0), key=lambda s: -s["speed"])
    for s in corners[:2]:
        s["track_limits"] = True
    longest = max((s for s in segments if s["kind"] == 0), key=lambda s: s["length"])
    activation = (longest["start"] + 50.0) % total
    detection = (activation - 450.0) % total

    track = {
        "name": name,
        "overtake_detection_m": round(detection),
        "overtake_activation_m": round(activation),
        "marshal_sectors": max(12, min(30, round(total / 280.0))),
        "tyres": template_tyres,
        "comment": "PRIVATE (gitignored). Generated by core/tools/trackgen/convert_circuits.py from f1-circuits (MIT, "
                   "Tomislav Bacinger), source id %s. Corner speeds from the radius, a sim approximation." % props["id"],
        "segments": [],
    }
    for s in segments:
        seg = {"name": s["name"], "length_m": round(s["length"], 1), "speed_kmh": round(s["speed"]), "overtake": s["overtake"],
               "sector": s["sector"], "turn_deg": round(math.degrees(s["turn"]), 2)}
        if s.get("track_limits"):
            seg["track_limits"] = True
        track["segments"].append(seg)
    report = "%-8s %-40s %5.0f m -> %6.0f m, %3d segments, closure %5.1f m -> %4.1f m, first segment %5.0f m, %3.0f..%3.0f km/h" % (
        props["id"], name, props.get("length", 0), total, len(segments), before, after, segments[0]["length"],
        min(s["speed"] for s in segments), max(s["speed"] for s in segments))
    return track, report


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--geojson", required=True)
    parser.add_argument("--names", required=True, help="JSON {\"names\": {id: name}}")
    parser.add_argument("--template", required=True, help="track JSON whose tyres are copied")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    features = json.load(open(args.geojson, encoding="utf-8"))["features"]
    names = json.load(open(args.names, encoding="utf-8"))["names"]
    tyres = json.load(open(args.template, encoding="utf-8"))["tyres"]
    os.makedirs(args.out, exist_ok=True)
    for feature in features:
        cid = feature["properties"]["id"]
        if feature["geometry"]["type"] != "LineString" or cid not in names:
            print("skipped", cid)
            continue
        track, report = convert(feature, names[cid], tyres)
        with open(os.path.join(args.out, slug(names[cid]) + ".json"), "w", encoding="utf-8") as f:
            json.dump(track, f, indent=1, ensure_ascii=False)
        print(report)


if __name__ == "__main__":
    main()

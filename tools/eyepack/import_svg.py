#!/usr/bin/env python3
"""Reads a design sheet of eyes (SVG) and writes the Square pack's numbers into eyes/square.json.

    python3 tools/eyepack/import_svg.py design/square-reference.svg [--dry]

The sheet is a grid of mood groups (<g id="happy" data-expression="happy">), each with a left and a
right eye. For every eye this finds the body path, the dark pupil, the lid line above it, and turns
their geometry into the renderer's parameters (see eyes_glass_t in eyes_raster.h). Moods the sheet
draws in another way (chevrons, plain lines) are left as they are in the JSON.
"""
import json, math, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEVICE_PER_UNIT = 466.0 / 256.0   # one sheet unit in device pixels (the sheet's cell is 256 wide)
BASE = 170.0                      # EYE_W / EYE_H in expressive_eyes.c
K = DEVICE_PER_UNIT / BASE        # sheet units -> the renderer's fractions of the base eye
SAG = 0.68                        # chord-to-centre share of the renderer's bow term at the top edge
BOWEND = 1.0 - SAG                # share of it already present at the body's top corners


def nums(d):
    return [float(x) for x in re.findall(r"-?\d+(?:\.\d+)?", d)]


def bez(p0, p1, p2, p3, n=24):
    out = []
    for i in range(n + 1):
        t = i / n; u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
    return out


def sample(d):
    """Points along a path made of M, L, Q, C and Z (absolute coordinates)."""
    pts, cur, start = [], (0, 0), (0, 0)
    for cmd, args in re.findall(r"([MLQCZHV])([^MLQCZHV]*)", d):
        a = nums(args)
        if cmd == "M": cur = start = (a[0], a[1]); pts.append(cur)
        elif cmd == "L": cur = (a[0], a[1]); pts.append(cur)
        elif cmd == "H": cur = (a[0], cur[1]); pts.append(cur)
        elif cmd == "V": cur = (cur[0], a[0]); pts.append(cur)
        elif cmd == "Q":
            for i in range(24 + 1):
                t = i / 24; u = 1 - t
                pts.append((u * u * cur[0] + 2 * u * t * a[0] + t * t * a[2], u * u * cur[1] + 2 * u * t * a[1] + t * t * a[3]))
            cur = (a[2], a[3])
        elif cmd == "C":
            pts += bez(cur, (a[0], a[1]), (a[2], a[3]), (a[4], a[5])); cur = (a[4], a[5])
        elif cmd == "Z": cur = start
    return pts


def r3(v):
    return round(v, 3)


def eye_params(group, left):
    """The glass parameters of one eye group, or None when the sheet draws it another way."""
    paths = re.findall(r"<path\b([^>]*)>", group)
    ells = re.findall(r"<ellipse\b([^>]*)>", group)
    body = next((a for a in paths if 'fill="url(#blue)"' in a), None)
    hat = next((a for a in paths if 'fill="url(#closed)"' in a), None)
    nose = 1.0 if left else -1.0                       # the nose is at +x for the left eye
    pupil_kind = "disc"
    pupil = None
    if ells:
        e = {k: float(v) for k, v in re.findall(r'\b(cx|cy|rx|ry)="([-\d.]+)"', ells[0])}
        pupil = e
    heart = next((a for a in paths if 'fill="#020c19"' in a), None)
    spiral = 'stroke="#06283c"' in group
    if hat:
        d = re.search(r'd="([^"]+)"', hat).group(1)
        n = nums(d)
        # M x y C .. (0 top) C .. (x y) C .. (0 apex) C .. Z : a dome with a hollow underside
        tip_l, top, tip_r = (n[0], n[1]), n[7], (n[12], n[13])
        apex = n[19]
        hw = (abs(tip_l[0]) + abs(tip_r[0])) / 2
        box_top, bottom = top, (tip_l[1] + tip_r[1]) / 2
        hh = (bottom - box_top) / 2
        bow = (bottom - apex) / SAG
        bot0 = bottom + BOWEND * bow
        return {"w": r3(2 * hw * K), "h": r3(2 * hh * K), "corner": 0.05, "round_top": 1.0, "lid_gap": 0,
                "bottom": {"cover": r3((bottom - bot0) / (2 * hh)), "bow": r3(bow / hh)}}
    curve = next((a for a in paths if 'stroke="#8bffff"' in a and 'stroke-width="3.1"' in a), None)
    if curve and not body:      # a closed eye drawn as one curved line: a thin crescent
        ln = nums(re.search(r'd="([^"]+)"', curve).group(1))
        ya, yb, qy = ln[1], ln[5], ln[3]
        hw = (abs(ln[0]) + abs(ln[4])) / 2
        t = 3.1
        mid = 0.25 * (ya + yb) + 0.5 * qy
        chord = (ya + yb) / 2
        bow = (mid - chord) / SAG
        top0 = chord - BOWEND * bow
        box_top, bottom = min(ya, yb), mid + t
        hh = (bottom - box_top) / 2
        return {"w": r3(2 * hw * K), "h": r3(2 * hh * K), "corner": 0.05, "lid_gap": 0,
                "top": {"cover": r3((top0 - box_top) / (2 * hh)), "slope": 0.0, "bow": r3(bow / hh)},
                "bottom": {"cover": r3((bottom - (top0 + t)) / (2 * hh)), "slope": 0.0, "bow": r3(-bow / hh)}}
    if not body:
        return None
    d = re.search(r'd="([^"]+)"', body).group(1)
    n = nums(d)
    cmds = re.findall(r"[MQCZ]", d)
    if cmds[:3] == ["M", "Q", "C"] and len(n) == 18:
        xa, ya, qx, qy, xb, yb = n[0:6]
        bottom = n[11]
        side = (abs(n[6]) + abs(n[14])) / 2
        hw = (abs(xa) + abs(xb) + 2 * side) / 4
        # the bottom is one cubic from the top-right corner: fit the ellipse that passes its midpoint
        mx = .125 * n[4] + .375 * n[6] + .375 * n[8] + .125 * n[10]
        my = .125 * n[5] + .375 * n[7] + .375 * n[9] + .125 * n[11]
        corner_h = (bottom - my) / (1 - math.sqrt(max(0.0, 1 - (mx / hw) ** 2)))
        mid = 0.25 * (ya + yb) + 0.5 * qy
        chord = (ya + yb) / 2
        bow = (mid - chord) / SAG
        top0 = chord - BOWEND * bow
        slope = (yb - ya) / (abs(xa) + abs(xb)) * nose
        box_top = min(top0, top0 + bow, ya, yb)
        hh = (bottom - box_top) / 2
        cy = (bottom + box_top) / 2
        g = {"w": r3(2 * hw * K), "h": r3(2 * hh * K), "corner": r3(corner_h / hw), "lid_gap": 0.26}
        g["top"] = {"cover": r3((top0 - box_top) / (2 * hh)), "slope": r3(slope), "bow": r3(bow / hh)}
        line = next((a for a in paths if 'stroke="#73faff"' in a), None)
        if line:
            ln = nums(re.search(r'd="([^"]+)"', line).group(1))
            line_mid = 0.25 * (ln[1] + ln[5]) + 0.5 * ln[3]
            g["lid_gap"] = r3(max(0.05, (mid - line_mid) / hw))
            g["lid_len"] = r3(abs(ln[4] - ln[0]) / 2 / hw)
        if pupil:
            g["pupil"] = {"size": r3(pupil["rx"] / hw), "x": r3(pupil["cx"] / hw * nose), "y": r3((pupil["cy"] - cy) / hh)}
            # The sheet lets heavy lids hide a pupil; on the device it must stay visible, so keep most of it under the lid.
            r = pupil["rx"] * 0.875 / hh
            lid_y = -1 + 2 * g["top"]["cover"] + g["top"]["slope"] * g["pupil"]["x"] * hw / hh + g["top"]["bow"] * 0.5
            g["pupil"]["y"] = r3(max(g["pupil"]["y"], lid_y + 0.45 * r))
        elif heart:
            hn = nums(re.search(r'd="([^"]+)"', heart).group(1))
            g["pupil"] = {"size": 0.55, "x": r3(13 / hw * nose), "y": r3((2 - cy) / hh), "kind": "heart"}
        elif spiral:
            g["pupil"] = {"size": 0.62, "x": 0.0, "y": r3((8 - cy) / hh), "kind": "rings"}
        return g
    if len(n) >= 24 and cmds[:2] == ["M", "C"]:   # the tall rounded body of surprised / startled
        pts = sample(d)
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        hw = (max(xs) - min(xs)) / 2
        box_top, bottom = min(ys), max(ys)
        hh = (bottom - box_top) / 2
        cy = (bottom + box_top) / 2
        g = {"w": r3(2 * hw * K), "h": r3(2 * hh * K), "corner": r3(min(1.0, hh / hw)), "round_top": 1.0, "lid_gap": 0.1}
        line = next((a for a in paths if 'stroke="#73faff"' in a), None)
        if line:
            ln = nums(re.search(r'd="([^"]+)"', line).group(1))
            line_mid = 0.25 * (ln[1] + ln[5]) + 0.5 * ln[3]
            g["lid_gap"] = r3(max(0.05, (box_top - line_mid) / hw))
            g["lid_len"] = r3(abs(ln[4] - ln[0]) / 2 / hw)
            chord = (ln[1] + ln[5]) / 2
            g["top"] = {"cover": 0.0, "slope": 0.0, "bow": r3((line_mid - chord) / SAG / hh)}
        if pupil:
            g["pupil"] = {"size": r3(pupil["rx"] / hw), "x": r3(pupil["cx"] / hw * nose), "y": r3((pupil["cy"] - cy) / hh)}
        return g
    return None


def accessories(group):
    """Shapes drawn in a mood group outside its two eye groups: the sweat drop, the sparkles, the question mark."""
    depth, count = 0, 0
    for m in re.finditer(r'<g\b[^>]*>|</g>|<(path|circle|rect|ellipse|text|polygon)\b[^>]*>', group):
        t = m.group(0)
        if t.startswith("<g"): depth += 1
        elif t == "</g>": depth -= 1
        elif depth == 1 and not (m.group(1) == "text" and 'font-size="18"' in t):
            count += 1
    return count


def main():
    src = sys.argv[1]
    dry = "--dry" in sys.argv
    svg = open(src).read()
    groups = re.split(r'(?=<g id="[a-z-]+" data-expression)', svg)[1:]
    path = os.path.join(ROOT, "eyes", "square.json")
    pack = json.load(open(path))
    by_id = {e["id"]: e for e in pack["expressions"]}
    done, skipped = [], []
    for grp in groups:
        mood = re.match(r'<g id="([^"]+)"', grp).group(1)
        eyes = re.split(r'(?=<g transform="translate\()', grp)[1:]
        if mood not in by_id or len(eyes) < 2:
            skipped.append(mood); continue
        gl = eye_params(eyes[0], True)
        gr = eye_params(eyes[1], False)
        if not gl or not gr:
            skipped.append(mood); continue
        e = by_id[mood]
        if e.get("hand_made"):   # drawn by hand in the JSON: the sheet is not the source for this mood
            skipped.append(mood + " (hand made)"); continue
        has = lambda eye: eye != "same" and eye.get("accent", "none") != "none"
        if accessories(grp) and not (has(e["left"]) or has(e["right"])):
            print(f"WARNING {mood}: the sheet draws {accessories(grp)} accessory shape(s) but the JSON has no accent for it")
        if e["right"] == "same":
            e["right"] = json.loads(json.dumps(e["left"]))
        e["left"]["glass"], e["right"]["glass"] = gl, gr
        if e["left"] == e["right"]:
            e["right"] = "same"
        done.append(mood)
    laugh = by_id.get("laugh")
    if laugh:   # the chevrons are drawn by the orb renderer's chevron shape
        laugh["left"]["glass"] = laugh["right"]["glass"] = {"orb": True, "w": 0.63, "h": 0.45}
        done.append("laugh")
    print("imported:", ", ".join(done))
    print("left as written:", ", ".join(skipped))
    if not dry:
        json.dump(pack, open(path, "w"), indent=1)
        open(path, "a").write("\n")


if __name__ == "__main__":
    main()

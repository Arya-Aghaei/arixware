#!/usr/bin/env python3
"""Measures how closely a pack's eyes match the reference art.

    python3 tools/eyepack/compare.py orb [--reference design/orb-reference.svg] [--theme 0]
    python3 tools/eyepack/compare.py fit orb [--write] [--skip dreamy,nervous]   # propose size changes; --write applies them

It renders every expression with the host preview (the same rasterizer the device runs), cuts the
matching cell out of the reference sheet, and compares the two silhouettes: overlap (IoU, with a 2 px tolerance) after
aligning their centres, and the size of the eyes. The result is a table; the exit status is 1 when a
mood falls under the pack's "min_iou", so a regression is caught instead of noticed on the device.
"""
import json, os, struct, subprocess, sys, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.environ.get("EYES_BUILD", "/tmp/arixware-eyes")


def catalog_ids():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import eyepack
    return eyepack.catalog()


def read_png(path):
    d = open(path, "rb").read()[8:]
    i, idat = 0, b""
    while i < len(d):
        n, = struct.unpack(">I", d[i:i + 4]); t = d[i + 4:i + 8]; c = d[i + 8:i + 8 + n]; i += 12 + n
        if t == b"IHDR": w, h, _, ct = struct.unpack(">IIBB", c[:10])
        if t == b"IDAT": idat += c
    raw = zlib.decompress(idat); bpp = {2: 3, 6: 4}[ct]; stride = w * bpp
    rows, prev, o = [], bytearray(stride), 0
    for _ in range(h):
        ft = raw[o]; line = bytearray(raw[o + 1:o + 1 + stride]); o += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0; b = prev[x]; c = prev[x - bpp] if x >= bpp else 0
            if ft == 1: line[x] = (line[x] + a) & 255
            elif ft == 2: line[x] = (line[x] + b) & 255
            elif ft == 3: line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif ft == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(line); prev = line
    return w, h, bpp, rows


def read_ppm(path):
    d = open(path, "rb").read(); parts = d.split(b"\n", 3)
    w, h = map(int, parts[1].split()); return w, h, parts[3]


def bright(r, g, b):  # a lit, coloured pixel: the eye, not the black background or grey caption text
    hi, lo = max(r, g, b), min(r, g, b)
    return hi > 150 and hi - lo > 70


def ref_mask(png, cell, col, row, size):
    w, h, bpp, rows = png
    x0, y0 = col * cell[0], int(row * cell[1]) + 25  # below the caption of the row above
    m = set()
    for y in range(y0, min(y0 + size[1], h)):
        r = rows[y]
        for x in range(x0, min(x0 + size[0], w)):
            if bright(r[x * bpp], r[x * bpp + 1], r[x * bpp + 2]): m.add((x - x0, y - y0))
    return m


def fw_mask(ppm, scale, size):
    w, h, data = ppm
    m = set()
    for y in range(size[1]):
        sy = int((y + 0.5) / scale) + 70  # the eyes sit in a band of the 466 px frame
        for x in range(size[0]):
            sx = int((x + 0.5) / scale)
            if sy >= h or sx >= w: continue
            k = (sy * w + sx) * 3
            if bright(data[k], data[k + 1], data[k + 2]): m.add((x, y))
    return m


def centroid(m):
    return (sum(p[0] for p in m) / len(m), sum(p[1] for p in m) / len(m)) if m else (0, 0)


def bbox(m):
    if not m: return (0, 0)
    xs = [p[0] for p in m]; ys = [p[1] for p in m]
    return (max(xs) - min(xs) + 1, max(ys) - min(ys) + 1)


def components(m, min_area=250):
    """The connected blobs of a mask, biggest first: the two eyes come first, accessories after."""
    seen, out = set(), []
    for p in m:
        if p in seen: continue
        stack, blob = [p], []
        seen.add(p)
        while stack:
            q = stack.pop(); blob.append(q)
            for d in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
                n = (q[0] + d[0], q[1] + d[1])
                if n in m and n not in seen:
                    seen.add(n); stack.append(n)
        if len(blob) >= min_area: out.append(blob)
    out.sort(key=len, reverse=True)
    return out


def eye_sizes(m):
    """(width, height) of the left and right eye blobs, ignoring rays, sparkles and other accessories."""
    blobs = sorted(components(m)[:2], key=lambda b: sum(p[0] for p in b) / len(b))
    sizes = []
    for b in blobs:
        xs = [p[0] for p in b]; ys = [p[1] for p in b]
        sizes.append((max(xs) - min(xs) + 1, max(ys) - min(ys) + 1))
    return sizes


def grow(m, r):
    """The mask thickened by r pixels: a tolerance so a thin line that is 1 px off still counts."""
    return {(x + dx, y + dy) for x, y in m for dx in range(-r, r + 1) for dy in range(-r, r + 1)}


def prepare(pack_name, args):
    opt = lambda k, d=None: args[args.index(k) + 1] if k in args else d
    pack = json.load(open(os.path.join(ROOT, "eyes", pack_name + ".json")))
    ref = pack.get("reference", {})
    ref_path = opt("--reference") or ref.get("image")
    if not ref_path: sys.exit(f"{pack_name}.json has no \"reference\" image; pass --reference")
    ref_path = ref_path if os.path.isabs(ref_path) else os.path.join(ROOT, ref_path)
    if not os.path.exists(ref_path):
        sys.exit(f"reference art not found: {ref_path}\nPut the design sheet there (or pass --reference).")
    grid = ref.get("grid", {"cols": 6, "cell": [256, 187]})
    cell, cols = grid["cell"], grid["cols"]
    if ref_path.endswith(".svg"):
        png_path = os.path.join(BUILD, pack_name + "-reference.png")
        subprocess.check_call(["rsvg-convert", "-w", str(cols * cell[0]), ref_path, "-o", png_path])
    else:
        png_path = ref_path
    return pack, ref, opt, read_png(png_path), cell, cols


def build_preview():
    exe = os.path.join(BUILD, "eyes_preview")
    src = os.path.join(ROOT, "tools", "eyes_preview")
    if not os.path.exists(os.path.join(BUILD, "CMakeCache.txt")):
        subprocess.check_call(["cmake", "-S", src, "-B", BUILD], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.check_call(["cmake", "--build", BUILD, "-j", "8"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return exe


def render_pack(pack_name, opt):
    exe = build_preview()
    out = os.path.join(BUILD, "frames-" + pack_name); os.makedirs(out, exist_ok=True)
    env = dict(os.environ, EYES_STYLE="1" if pack_name == "square" else "0", EYES_THEME=opt("--theme", "7" if pack_name == "square" else "0"))
    subprocess.check_call([exe, "--all", out], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out


def measure(pack, ref, opt, png, cell, cols, out):
    scale = cell[0] / 466.0
    size = (cell[0], 150)  # the eyes, not the captions around them
    tol = int(opt("--tolerance", ref.get("tolerance", 2)))
    data = []
    ids = catalog_ids()
    for i, e in enumerate(pack["expressions"]):
        a = ref_mask(png, cell, i % cols, i // cols, size)
        b = fw_mask(read_ppm(os.path.join(out, "eyes_%02d.ppm" % ids.index(e["id"]))), scale, size)
        ca, cb = centroid(a), centroid(b)
        dx, dy = round(ca[0] - cb[0]), round(ca[1] - cb[1])
        b2 = {(x + dx, y + dy) for x, y in b}
        ga, gb = grow(a, tol), grow(b2, tol)
        # overlap of the thickened masks, scored against the originals: a stroke 1-2 px off is fine
        iou = (len(a & gb) + len(b2 & ga)) / max(1, len(a) + len(b2))
        data.append({"label": e["label"], "iou": iou, "ref": eye_sizes(a), "pack": eye_sizes(b)})
    return data


def report(data, floor):
    print(f"{'mood':14s} {'IoU':>5s}  {'reference eyes':>16s}  {'pack eyes':>14s}")
    fmt = lambda s: " ".join(f"{w}x{h}" for w, h in s) or "-"
    for d in sorted(data, key=lambda d: d["iou"]):
        flag = "  <-- below %.2f" % floor if d["iou"] < floor else ""
        print(f"{d['label']:14s} {d['iou']:5.2f}  {fmt(d['ref']):>16s}  {fmt(d['pack']):>14s}{flag}")
    ious = [d["iou"] for d in data]
    print(f"mean IoU {sum(ious) / len(ious):.3f}; {sum(1 for v in ious if v < floor)} of {len(ious)} below {floor:.2f}")
    return all(v >= floor for v in ious)


def fit(pack_name, args):
    """Propose (or, with --write, apply) w and h changes that make each eye the size of the reference.

    Sizes only: it cannot judge shape. Accessories that touch an eye (clouds, rays) can fool it, so by
    default it only prints what it would change, and you review the result on the sheet."""
    pack_path = os.path.join(ROOT, "eyes", pack_name + ".json")
    write = "--write" in args
    rounds = int(args[args.index("--rounds") + 1]) if "--rounds" in args else (3 if write else 1)
    skip = set(args[args.index("--skip") + 1].split(",")) if "--skip" in args else set()
    for r in range(rounds):
        pack, ref, opt, png, cell, cols = prepare(pack_name, args)
        subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "eyepack", "eyepack.py"), "generate"], stdout=subprocess.DEVNULL)
        out = render_pack(pack_name, opt)
        data = measure(pack, ref, opt, png, cell, cols, out)
        changed = 0
        for e, d in zip(pack["expressions"], data):
            if len(d["ref"]) != 2 or len(d["pack"]) != 2 or e["id"] in skip: continue
            eyes = [e["left"], e["right"] if e["right"] != "same" else None]
            for k, (sr, sp) in enumerate(zip(d["ref"], d["pack"])):
                eye = eyes[k]
                if eye is None or not sp[0] or not sp[1]: continue
                rw, rh = sr[0] / sp[0], sr[1] / sp[1]
                if abs(rw - 1) < 0.04 and abs(rh - 1) < 0.04: continue
                rw, rh = max(0.7, min(1.4, rw)), max(0.7, min(1.4, rh))
                if eye.get("shape") == "worried":
                    rh = 1.0  # the lobes are sized by the width; only w changes how big the eye is
                nw, nh = round(min(1.9, eye["w"] * rw), 3), round(min(1.9, eye["h"] * rh), 3)
                if not write:
                    print(f"{e['label']:14s} {'left' if k == 0 else 'right'}: w {eye['w']} -> {nw}, h {eye['h']} -> {nh}")
                eye["w"], eye["h"] = nw, nh
                changed += 1
        mean = sum(d["iou"] for d in data) / len(data)
        print(f"round {r + 1}: mean IoU {mean:.3f}; {changed} eyes " + ("adjusted" if write else "would change (nothing written; add --write)"))
        if not write or not changed: break
        json.dump(pack, open(pack_path, "w"), indent=1)
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "eyepack", "eyepack.py"), "generate"], stdout=subprocess.DEVNULL)


def main():
    args = sys.argv[1:]
    if not args: sys.exit(__doc__)
    if args[0] == "fit":
        return fit(args[1], args[2:])
    pack_name = args[0]
    pack, ref, opt, png, cell, cols = prepare(pack_name, args)
    out = render_pack(pack_name, opt)
    data = measure(pack, ref, opt, png, cell, cols, out)
    floor = float(opt("--min", ref.get("min_iou", 0.6)))
    sys.exit(0 if report(data, floor) else 1)


if __name__ == "__main__":
    main()

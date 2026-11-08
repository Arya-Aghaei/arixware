#!/usr/bin/env python3
"""Eye packs: the design of every mood lives in eyes/*.json, and this tool turns it into C.

    python3 tools/eyepack/eyepack.py check       # validate the packs
    python3 tools/eyepack/eyepack.py generate    # check, then write components/eyes_renderer/generated/
    python3 tools/eyepack/eyepack.py verify      # fail if the generated C is out of date (runs in ctest)

The firmware never hand-codes an expression: expressive_eyes.c includes the generated tables. Change a
number in eyes/<pack>.json, regenerate, and the host preview and the device show exactly that.
See docs/EYE_PACKS.md.
"""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK_FILES = ["orb", "square"]           # the order is the style number in the firmware
OUT_DIR = os.path.join(ROOT, "components", "eyes_renderer", "generated")
HEADER = os.path.join(ROOT, "components", "eyes_renderer", "include", "eyes_expression.h")

ORB_SHAPES = ["capsule", "arch", "chevron", "crescent", "spiral", "half", "slope", "heart", "worried",
              "sleep", "oval", "egg", "lens"]
ACCENTS = ["none", "rays", "sparkles", "side_rays", "star", "question", "hearts", "blush", "nervous",
           "drop", "zzz", "clouds", "top_rays", "ellipsis", "bulb", "target", "anger_mark", "tears",
           "stress", "eyebags", "tongue", "glitch"]
PUPILS = ["disc", "heart", "rings"]
THEMES = ["Amber", "Rose", "Violet", "Ice", "Mint", "Lime", "Mono", "Cyan"]

ORB_FIELDS = {"shape": None, "w": (0.2, 2.0), "h": (0.1, 2.0), "openness": (0.0, 1.0), "dir": (-2, 2), "accent": None}
# The square pack is written with the same vocabulary as the orb pack. Each eye may also override what
# the renderer derives from it: lids, pupil and corner (see derive_square).
SQUARE_FIELDS = dict(ORB_FIELDS, top=None, bottom=None, pupil=None, corner=(0.05, 1.0), glass=None)
LID_FIELDS = {"cover": (0.0, 0.6), "slope": (-1.5, 1.5), "bow": (-1.5, 1.5)}
PUPIL_FIELDS = {"size": (0.0, 1.0), "x": (-1.0, 1.0), "y": (-1.0, 1.0), "kind": None}


def die(msg):
    print("eyepack: " + msg, file=sys.stderr)
    sys.exit(1)


def catalog():
    text = open(HEADER).read().split("typedef enum")[1].split("}")[0]
    ids = [m.lower().replace("_", "-") for m in re.findall(r"EYES_EXPRESSION_([A-Z_]+),", text)]
    return [i for i in ids if i != "count"]


def num(where, key, v, rng):
    if not isinstance(v, (int, float)) or isinstance(v, bool):
        die(f"{where}.{key} must be a number")
    if rng and not (rng[0] <= v <= rng[1]):
        die(f"{where}.{key} = {v} is outside {rng[0]}..{rng[1]}")
    return float(v)


def known(where, obj, allowed):
    for k in obj:
        if k not in allowed:
            die(f"{where}: unknown field '{k}' (allowed: {', '.join(sorted(allowed))})")


GLASS_KEYS = {"w": (0.2, 0.95), "h": (0.08, 1.0), "corner": (0.05, 1.5), "round_top": (0.0, 1.0),
              "lid_gap": (0.0, 0.6), "lid_len": (0.5, 1.4)}
GLASS_LID = {"cover": (-1.0, 1.2), "slope": (-1.0, 1.0), "bow": (-2.5, 2.5)}
GLASS_PUPIL = {"size": (0.0, 1.0), "x": (-1.0, 1.0), "y": (-2.0, 2.0)}


def check_glass(where, g):
    """A square-pack eye written as the renderer's numbers. Mistakes that would only show on the device
    (an eye too big for its sprite, a pupil hidden under the lid) fail here instead."""
    known(where, g, set(GLASS_KEYS) | {"top", "bottom", "pupil", "orb"})
    if g.get("orb"):   # drawn with the orb renderer (the laugh's chevrons): w and h size the orb shape
        return
    for k in ("w", "h"):
        if k not in g: die(f"{where} needs {k}")
    for k, rng in GLASS_KEYS.items():
        if k in g: num(where, k, g[k], rng)
    for lid in ("top", "bottom"):
        if lid in g:
            known(f"{where}.{lid}", g[lid], set(GLASS_LID))
            for k, v in g[lid].items(): num(f"{where}.{lid}", k, v, GLASS_LID[k])
    if "pupil" in g:
        known(f"{where}.pupil", g["pupil"], set(GLASS_PUPIL) | {"kind"})
        for k, v in g["pupil"].items():
            if k != "kind": num(f"{where}.pupil", k, v, GLASS_PUPIL[k])
        if g["pupil"].get("kind", "disc") not in PUPILS: die(f"{where}.pupil.kind must be one of {PUPILS}")
        # how much of the pupil sits below the upper lid (units of the half height, y down)
        t, p = g.get("top", {}), g["pupil"]
        hh, hw = g["h"] / 2, g["w"] / 2
        lid = -1 + 2 * t.get("cover", 0) + t.get("slope", 0) * p.get("x", 0) * hw / hh + t.get("bow", 0) * 0.5
        r = p.get("size", 0) * hw * 0.875 / hh
        if r > 0 and p.get("y", 0) + r * 0.1 < lid:
            die(f"{where}: the pupil is hidden under the upper lid (pupil.y {p.get('y', 0):.2f}, lid at {lid:.2f}); "
                f"lower pupil.y or reduce top.cover")
    # the whole eye must fit the 256 px sprite: 170 px base, plus bloom and the lid line
    if g["w"] * 170 * 1.18 + 44 > 256 or g["h"] * 170 + 44 + g.get("lid_gap", 0) * g["w"] * 85 > 256:
        die(f"{where}: the eye is too big for its sprite (w {g['w']}, h {g['h']})")


def check_eye(where, e, fields):
    if "glass" in e: check_glass(where + ".glass", e["glass"])
    known(where, e, fields)
    if e.get("shape") not in ORB_SHAPES:
        die(f"{where}.shape must be one of {ORB_SHAPES}")
    if "w" not in e or "h" not in e:
        die(f"{where} needs w and h")
    for k in ("w", "h", "openness", "dir", "corner"):
        if k in e: num(where, k, e[k], fields[k])
    if e.get("accent", "none") not in ACCENTS:
        die(f"{where}.accent must be one of {ACCENTS}")
    for lid in ("top", "bottom"):
        if lid in e:
            known(f"{where}.{lid}", e[lid], LID_FIELDS)
            for k, v in e[lid].items(): num(f"{where}.{lid}", k, v, LID_FIELDS[k])
    if "pupil" in e:
        known(f"{where}.pupil", e["pupil"], PUPIL_FIELDS)
        for k in ("size", "x", "y"):
            if k in e["pupil"]: num(f"{where}.pupil", k, e["pupil"][k], PUPIL_FIELDS[k])
        if e["pupil"].get("kind", "disc") not in PUPILS:
            die(f"{where}.pupil.kind must be one of {PUPILS}")


def load_packs():
    ids = catalog()
    packs = []
    for name in PACK_FILES:
        path = os.path.join(ROOT, "eyes", name + ".json")
        if not os.path.exists(path): die(f"missing {path}")
        pack = json.load(open(path))
        known(name, pack, {"pack", "title", "style", "default_theme", "reference", "expressions", "fallbacks"})
        if pack.get("style") != name: die(f"{name}.json: style must be '{name}'")
        if pack.get("default_theme") not in THEMES: die(f"{name}.json: default_theme must be one of {THEMES}")
        own = {}
        for e in pack["expressions"]:
            where = f"{name}.{e.get('id')}"
            known(where, e, {"id", "label", "left", "right", "hand_made"})
            if e["id"] not in ids: die(f"{where}: unknown mood id; the catalog is {ids}")
            if e["id"] in own: die(f"{where}: defined twice")
            fields = ORB_FIELDS if name == "orb" else SQUARE_FIELDS
            check_eye(where + ".left", e["left"], fields)
            if e["right"] != "same": check_eye(where + ".right", e["right"], fields)
            own[e["id"]] = e
        fb = pack.get("fallbacks", {})
        for i in ids:
            if i not in own:
                if i not in fb: die(f"{name}.json: mood '{i}' is neither defined nor listed in fallbacks")
                if fb[i] not in own: die(f"{name}.json: fallback '{i}' -> '{fb[i]}' must name a mood the pack defines")
        for i in fb:
            if i in own: die(f"{name}.json: '{i}' is both defined and a fallback")
        pack["_own"] = own
        packs.append(pack)
    return ids, packs


def resolved(pack, mood):
    """The expression a pack shows for a catalog mood: its own, or its fallback's."""
    return pack["_own"].get(mood) or pack["_own"][pack["fallbacks"][mood]]


def f(v):
    v = float(v)
    s = repr(v)
    if "e" in s: s = "%.6f" % v
    if s.endswith(".0"): return s + "f"
    if "." not in s: s += ".0"
    return s + "f"


def accent_c(e):
    return "EYES_ACCENT_" + e.get("accent", "none").upper()


def derive_square(e):
    """The square renderer's numbers for an eye written in the orb vocabulary, plus any overrides."""
    if "glass" in e:   # numbers taken from the reference art: used as they are
        g = dict(e["glass"])
        if "accent" in e: g["accent"] = e["accent"]
        return g
    shape, w, h, d = e["shape"], e["w"], e["h"], int(e.get("dir", 0))
    # Widths follow the reference art (every eye is about as wide as the idle eye); h sets the body height.
    g = {"corner": 0.40}
    pupil = {"size": 0.46, "x": 0.0, "y": 0.02}
    top = bottom = None
    gw = w * 0.64
    if shape == "capsule":
        g.update(w=gw, h=h * 0.78)
    elif shape == "egg":
        g.update(w=gw, h=h * 0.78); pupil.update(x=0.3, y=-0.22)
    elif shape == "oval":
        g.update(w=w * 0.6, h=h * 0.72); pupil = {"size": 0.26, "x": 0.0, "y": 0.0}
    elif shape == "half":
        g.update(w=gw, h=0.66)
        top = {"cover": 0.42, "slope": 0.2 if d else 0.0}
        pupil = {"size": 0.46, "x": -0.25 if d else 0.0, "y": 0.18}
    elif shape == "slope":
        g.update(w=gw, h=0.66, corner=0.34)
        top = {"cover": 0.44, "slope": 0.7}
        pupil = {"size": 0.44, "x": 0.1, "y": 0.2}
    elif shape in ("arch", "crescent"):
        if d == 2:   # sad: the lid droops toward the outside
            g.update(w=gw * 0.96, h=0.68)
            top = {"cover": 0.3, "slope": -0.7}
            pupil = {"size": 0.52, "x": 0.0, "y": 0.28}
        else:        # happy: cheeks lift the lower lid
            g.update(w=gw, h=0.62)
            bottom = {"cover": 0.34, "bow": 0.6}
            pupil = None
    elif shape == "worried":
        g.update(w=gw * 0.94, h=h * 0.7)
        top = {"cover": 0.14, "slope": -0.45}
        pupil = {"size": 0.44, "x": 0.0, "y": 0.12}
    elif shape == "heart":
        g.update(w=gw, h=h * 0.78); pupil = {"size": 0.8, "x": 0.0, "y": 0.05, "kind": "heart"}
    elif shape in ("sleep", "lens"):
        g.update(w=gw, h=0.6)
        top = {"cover": 0.5}; bottom = {"cover": 0.5}; pupil = None
    elif shape == "spiral":
        g.update(w=gw, h=h * 0.76); pupil = {"size": 0.9, "x": 0.0, "y": 0.0, "kind": "rings"}
    elif shape == "chevron":
        g.update(w=0.66, h=0.62)
        top = {"cover": 0.5, "bow": -0.35}; bottom = {"cover": 0.52, "bow": 0.7}; pupil = None
    for k in ("w", "h"):
        g[k] = round(g[k], 3)
    if top: g["top"] = top
    if bottom: g["bottom"] = bottom
    if pupil: g["pupil"] = pupil
    for k in ("top", "bottom", "pupil", "corner"):       # the pack's own overrides win
        if k in e:
            g[k] = {**g.get(k, {}), **e[k]} if isinstance(e[k], dict) else e[k]
    if g.get("pupil", {}).get("size", 1) == 0:
        g.pop("pupil")
    for k in ("w", "h"):
        if k in e and "square_" + k in e: g[k] = e["square_" + k]
    if "accent" in e: g["accent"] = e["accent"]
    return g


def generate(ids, packs, out_dir=None):
    orb, square = packs
    out = ["/* Generated by tools/eyepack/eyepack.py from the JSON packs in eyes/. Do not edit; change the JSON. */", ""]
    out.append("/* Orb pack, one entry per catalog mood (moods the pack lacks show their fallback). */")
    out.append("static const expression_t EXPRESSIONS[] = {")
    for i in ids:
        e = resolved(orb, i)
        def eye(d):
            return "{ EYES_SHAPE_%s, %s, %s, %s, %d, %s }" % (d["shape"].upper(), f(d["w"]), f(d["h"]),
                                                              f(d.get("openness", 1.0)), int(d.get("dir", 0)), accent_c(d))
        r = e["left"] if e["right"] == "same" else e["right"]
        out.append("    /* %s */ { .left = %s,\n      .right = %s }," % (i, eye(e["left"]), eye(r)))
    out.append("};")
    out.append("")
    out.append("/* Square pack: { w, h, corner, top{cover, slope, bow}, bottom{...}, pupil{size, x, y}, round_top, lid_gap, lid_len, pupil kind }. */")
    out.append("static const glass_pair_t GLASS[] = {")
    kinds = {"disc": 0, "heart": 1, "rings": 2}
    accents = []
    for i in ids:
        e = resolved(square, i)
        def eye(d):
            g = derive_square(d)
            t, b, p = g.get("top", {}), g.get("bottom", {}), g.get("pupil", {})
            vals = [-g["w"] if g.get("orb") else g["w"], g["h"], g.get("corner", 0.4), t.get("cover", 0), t.get("slope", 0), t.get("bow", 0),
                    b.get("cover", 0), b.get("slope", 0), b.get("bow", 0), p.get("size", 0), p.get("x", 0), p.get("y", 0), g.get("round_top", 0), g.get("lid_gap", 0.26), g.get("lid_len", 1.18)]
            return "{ " + ", ".join(f(v) for v in vals) + ", %d }" % kinds[p.get("kind", "disc")]
        r = e["left"] if e["right"] == "same" else e["right"]
        out.append("    /* %s */ { %s,\n      %s }," % (i, eye(e["left"]), eye(r)))
        accents.append((accent_c(e["left"]), accent_c(r)))
    out.append("};")
    out.append("")
    out.append("/* The square pack's own accessories (rays, sparkles ...), left and right eye. */")
    out.append("static const eyes_accent_t GLASS_ACCENT[][2] = {")
    for a in accents:
        out.append("    { %s, %s }," % a)
    out.append("};")
    out.append("")
    # catalog metadata: ids, labels (the first pack that defines a mood names it), and each pack's own mood list
    labels = {}
    for p in packs:
        for e in p["expressions"]:
            labels.setdefault(e["id"], e.get("label", e["id"]))
    meta = ["/* Generated by tools/eyepack/eyepack.py from the JSON packs in eyes/. Do not edit; change the JSON. */", "",
            "static const char *const PACK_TITLES[] = { %s };" % ", ".join('"%s"' % p["title"] for p in packs),
            "static const char *const PACK_THEMES[] = { %s };" % ", ".join('"%s"' % p["default_theme"] for p in packs),
            "static const char *const MOOD_IDS[] = { %s };" % ", ".join('"%s"' % i for i in ids),
            "static const char *const MOOD_LABELS[] = { %s };" % ", ".join('"%s"' % labels[i] for i in ids), ""]
    own_lists = [[ids.index(e["id"]) for e in p["expressions"]] for p in packs]
    meta.append("static const unsigned char PACK_MOOD_COUNT[] = { %s };" % ", ".join(str(len(l)) for l in own_lists))
    meta.append("static const unsigned char PACK_MOODS[][%d] = {" % len(ids))
    for l in own_lists:
        meta.append("    { %s }," % ", ".join(str(x) for x in l))
    meta.append("};")
    meta.append("")
    target = out_dir or OUT_DIR
    os.makedirs(target, exist_ok=True)
    open(os.path.join(target, "eye_packs.inc"), "w").write("\n".join(out))
    open(os.path.join(target, "eye_pack_meta.inc"), "w").write("\n".join(meta))
    if not out_dir:
        print(f"wrote {target}/eye_packs.inc and eye_pack_meta.inc ({len(ids)} moods, {len(packs)} packs)")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "generate"
    ids, packs = load_packs()
    if cmd == "check":
        print("eyepack: packs are valid")
    elif cmd == "generate":
        generate(ids, packs)
    elif cmd == "verify":
        import tempfile, filecmp
        with tempfile.TemporaryDirectory() as tmp:
            generate(ids, packs, tmp)
            for name in ("eye_packs.inc", "eye_pack_meta.inc"):
                if not filecmp.cmp(os.path.join(tmp, name), os.path.join(OUT_DIR, name), shallow=False):
                    die(f"{name} is out of date: run tools/eyepack/eyepack.py generate")
        print("eyepack: generated tables match the JSON")
    else:
        die("usage: eyepack.py check|generate|verify")

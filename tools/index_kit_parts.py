"""Blender step that indexes every modular part of the third-party ship and station kits (US-T763).

Run by tools/index-kit-parts.ps1 or the game's `sg.IndexKitParts` console command (any Blender 3.3+, headless):
    blender -b --factory-startup --python tools/index_kit_parts.py -- <Cpp/SourceArt/ThirdParty dir> <out .json>

It reads every `*Modules.fbx` of Ultimate Spaceships Creator (USC/USC_FBX/<Set>/), Star Sparrow
(StarSparrow/StarSparrow_FBX/) and Space Station Creator (SSC/SpaceStationsCreator_FBX/<Set>/), as data only, and writes
Data/ModularParts.json: per part its role, style tags, bounds and attach sockets. The file holds names and numbers only,
nothing of the art itself, so it may be committed; the packs may not (docs/ThirdPartyArt.md).

Frame: every number is in the game's model frame (nose +Z, up +Y, +X starboard, the frame UscKit fits hulls to) in the
pack's metres, relative to the part's own pivot (the node origin the pack modelled it about, which is where the pack's
own Unity creator attaches it). The pack's FBX frame is nose -Y, up +Z: see to_model.

Sockets: each is a face of the part where a neighbour mates, as the middle of the patch of the part's own vertices that
lie on that face of its box (not the box face's middle, so a tapered nose or an offset flange is placed exactly), the
face's outward normal (the side the neighbour lies on: mate two sockets by turning one normal onto the other's reverse
and their positions together) and the patch's extent across the face (width along the first, height along the second
of the face's two other axes in x, y, z order).
"""

import json
import math
import os
import re
import sys

import bpy
from mathutils import Vector

ROLES = ["core", "bow", "stern", "engine", "wing", "turret", "hangar", "antenna", "armour", "connector", "detail"]

# Pack folder -> (pack id, scale class of its sets). Space Station Creator sets are station parts.
PACKS = {
    "USC": "usc",
    "StarSparrow": "starsparrow",
    "SSC": "ssc",
}
CAPITAL_SETS = {"GalacticLeopard", "VoidWhale", "CraizanStar"}

# Parts the import scripts already bring into /Game/ThirdParty, and the origin their mesh is about there.
# Kept in step with tools/usc_prepare.py MODULES (box-centred) and tools/import_starsparrow.py (about the pivot).
IMPORTED = {
    ("usc", "GalacticLeopard", "Parts"): ["Turret", "Gun", "Thruster", "Cargo", "Silo", "Cylinder"],
    ("usc", "VoidWhale", "Parts"): ["TurretBase", "Cannon", "Lazer", "Thruster", "Dish", "Engine"],
}

# Rules in priority order: the first whose keyword starts one of the name's words gives the role and tags.
RULES = [
    ("cockpit", "bow", ["cockpit"]),
    ("forepart", "bow", ["nose"]),
    ("hood", "bow", ["canopy"]),
    ("window", "bow", ["canopy"]),
    ("turret", "turret", ["mount"]),
    ("gattling", "turret", ["barrel", "gatling"]),
    ("machinegun", "turret", ["barrel"]),
    ("barrel", "turret", ["barrel"]),
    ("cannon", "turret", ["barrel", "cannon"]),
    ("gun", "turret", ["barrel"]),
    ("lazer", "turret", ["barrel", "laser"]),
    ("laser", "turret", ["barrel", "laser"]),
    ("weapon", "turret", ["barrel"]),
    ("plasma", "turret", ["mount", "pod"]),
    ("missile", "turret", ["missile"]),
    ("rocket", "turret", ["missile"]),
    ("silo", "turret", ["missile"]),
    ("ammunition", "turret", ["magazine"]),
    ("thrust", "engine", ["nozzle"]),
    ("exhaust", "engine", ["nozzle"]),
    ("engine", "engine", ["pod"]),
    ("antenna", "antenna", []),
    ("dish", "antenna", ["dish"]),
    ("sensor", "antenna", ["sensor"]),
    ("detector", "antenna", ["sensor"]),
    ("solar", "antenna", ["solar"]),
    ("pole", "antenna", ["mast"]),
    ("spike", "antenna", ["mast"]),
    ("hangar", "hangar", []),
    ("gate", "hangar", ["door"]),
    ("wing", "wing", []),
    ("fin", "wing", ["fin"]),
    ("tail", "stern", []),
    ("end", "stern", []),
    ("armor", "armour", ["plate"]),
    ("armour", "armour", ["plate"]),
    ("shield", "armour", ["shield"]),
    ("plate", "armour", ["plate"]),
    ("cover", "armour", ["plate"]),
    ("fender", "armour", ["plate"]),
    ("buffer", "armour", ["plate"]),
    ("shell", "armour", ["plate"]),
    ("side", "armour", ["plate"]),
    ("ridge", "armour", ["plate"]),
    ("corner", "armour", ["plate"]),
    ("connector", "connector", []),
    ("bridge", "core", ["bridge", "superstructure"]),
    ("control", "core", ["bridge", "superstructure"]),
    ("core", "core", []),
    ("body", "core", []),
    ("hull", "core", []),
    ("module", "core", []),
    ("link", "core", []),
    ("hold", "core", ["cargo"]),
    ("cargo", "core", ["cargo"]),
    ("storage", "core", ["cargo"]),
    ("tank", "core", ["tank"]),
    ("capsule", "core", ["tank"]),
    ("base", "core", []),
    ("arch", "core", []),
    ("part", "core", []),
    ("passage", "connector", ["corridor"]),
    ("truss", "connector", ["truss"]),
    ("beam", "connector", ["truss"]),
    ("pillar", "connector", []),
    ("joint", "connector", ["joint"]),
    ("hinge", "connector", ["joint"]),
    ("clamp", "connector", []),
    ("arm", "connector", ["arm"]),
    ("hand", "connector", ["arm", "claw"]),
    ("support", "connector", ["pylon"]),
    ("holder", "connector", ["pylon"]),
    ("fork", "connector", ["pylon"]),
    ("crank", "connector", ["arm"]),
    ("pipe", "connector", ["pipe"]),
    ("bend", "connector", ["pipe"]),
    ("extension", "connector", []),
    ("bar", "connector", []),
    ("cap", "connector", []),
]

# Per-set overrides where a word means something else in that set.
SET_RULES = {
    # Space Station Creator's hangar set: every bay, floor and frame is part of a hangar.
    "Hangars": [("arch", "hangar", ["bay"]), ("base", "hangar", ["deck"]), ("floor", "hangar", ["deck"]),
                ("extension", "hangar", ["bay"]), ("corner", "connector", [])],
    # Station parts: their cockpit is a control room on a station, not a ship's bow.
    "Bridge": [("cockpit", "core", ["bridge", "superstructure"]), ("window", "core", ["bridge", "superstructure"])],
}

FACES = [  # (name, axis, sign)
    ("fore", 2, 1), ("aft", 2, -1), ("top", 1, 1), ("bottom", 1, -1), ("starboard", 0, 1), ("port", 0, -1),
]
PATCH = 0.02  # vertices within this share of the part's extent along the axis lie on that face


def to_model(v):
    """The pack's frame (nose -Y, up +Z) to the game's model frame (nose +Z, up +Y): see UscKit's PackToModel."""
    return Vector((-v.x, v.z, -v.y))


def words(name):
    """AstroEagle_Wing_2 -> ['wing'] once the set prefix is gone; MissileLauncher -> ['missile', 'launcher']."""
    out = []
    for chunk in re.split(r"[_\s.]+", name):
        for w in re.findall(r"[A-Z]+(?=[A-Z][a-z]|\d|$)|[A-Z]?[a-z]+|[A-Z]+", chunk):
            out.append(w.lower())
    return out


def short_name(node, family):
    """The part's own name: GalacticLeopard_Parts_Turret -> Turret; Armor1 -> Armor1 (sets that name parts bare)."""
    name = node.split(".")[0]
    for prefix in (f"{family}_Main_", f"{family}_Parts_", f"{family}_"):
        if name.startswith(prefix):
            return name[len(prefix):], prefix[len(family) + 1:-1] or None
    return name, None


def classify(short, family):
    ws = words(short)
    rules = SET_RULES.get(family, []) + RULES
    for key, role, tags in rules:
        if any(w.startswith(key) for w in ws):
            return role, list(tags)
    return "detail", ["greeble"]


def patch(pts, axis, sign, mn, mx):
    """The middle and extent of the vertices that lie on the face of the box at <sign> along <axis>."""
    ext = mx[axis] - mn[axis]
    plane = mx[axis] if sign > 0 else mn[axis]
    eps = max(PATCH * ext, 1e-4)
    on = [p for p in pts if abs(p[axis] - plane) <= eps]
    others = [a for a in range(3) if a != axis]
    lo = [min(p[a] for p in on) for a in others]
    hi = [max(p[a] for p in on) for a in others]
    pos = [0.0, 0.0, 0.0]
    pos[axis] = plane
    for i, a in enumerate(others):
        pos[a] = 0.5 * (lo[i] + hi[i])
    normal = [0, 0, 0]
    normal[axis] = sign
    return pos, normal, [hi[0] - lo[0], hi[1] - lo[1]]


def sockets_of(role, tags, pts, mn, mx):
    faces = {name: patch(pts, axis, sign, mn, mx) for name, axis, sign in FACES}
    size = mx - mn
    named = []

    def add(name, face):
        named.append((name,) + faces[face])

    if role == "engine":
        add("mount", "fore")
        add("exhaust", "aft")
        for f in ("top", "bottom", "port", "starboard"):
            add(f, f)
    elif role == "wing":
        # Its span is its longest extent across the length: a flat wing spans x, a fin stands up along y.
        span = 0 if size.x >= size.y else 1
        lo_face, hi_face = ("port", "starboard") if span == 0 else ("bottom", "top")
        if abs(mn[span] + mx[span]) < 0.2 * size[span]:
            # Both sides in one mesh, straddling the pivot: it mounts across the hull at its middle.
            tags.append("paired")
            for f in ("top", "bottom", "fore", "aft") if span == 0 else ("port", "starboard", "fore", "aft"):
                add(f, f)
        else:
            # One-sided: its root is the edge nearest the pivot (the pack models it about its root).
            root, tip = (lo_face, hi_face) if abs(mn[span]) <= abs(mx[span]) else (hi_face, lo_face)
            tags.append("starboard" if (span == 0 and root == "port") else "port" if span == 0 else
                        "up" if root == "bottom" else "down")
            add("root", root)
            add("tip", tip)
            add("fore", "fore")
            add("aft", "aft")
        tags.append("flat" if span == 0 else "vertical")
    elif role == "turret":
        axis = max(range(3), key=lambda a: size[a])
        if size[axis] > 2.5 * max(size[a] for a in range(3) if a != axis):
            # A barrel, along its longest extent (some are modelled standing up): its breech is the end nearest the
            # pivot, its muzzle the far one.
            if "mount" in tags:
                tags.remove("mount")
            if "barrel" not in tags and "missile" not in tags:
                tags.append("barrel")
            lo, hi = {0: ("port", "starboard"), 1: ("bottom", "top"), 2: ("aft", "fore")}[axis]
            breech, muzzle = (lo, hi) if abs(mn[axis]) <= abs(mx[axis]) else (hi, lo)
            if axis != 2:
                tags.append("vertical" if axis == 1 else "sideways")
            add("breech", breech)
            add("muzzle", muzzle)
            if axis == 2:
                add("base", "bottom")
        else:
            add("base", "bottom")
            add("top", "top")
            add("fore", "fore")
            add("aft", "aft")
    elif role == "antenna":
        # It stands on the face of its longest extent nearest the pivot and points away.
        axis = max(range(3), key=lambda a: size[a])
        lo, hi = {0: ("port", "starboard"), 1: ("bottom", "top"), 2: ("aft", "fore")}[axis]
        base, tip = (lo, hi) if abs(mn[axis]) <= abs(mx[axis]) else (hi, lo)
        add("base", base)
        add("tip", tip)
    else:
        for f, _, _ in FACES:
            add(f, f)
    return named


def r(x):
    v = round(x, 4)
    return 0.0 if v == 0 else v


def index_file(path, pack, family, rel, parts):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    for o in sorted((o for o in bpy.data.objects if o.type == "MESH"), key=lambda o: o.name):
        me = o.data
        me.calc_loop_triangles()
        pivot = o.matrix_world.translation.copy()
        pts = [to_model(o.matrix_world @ v.co - pivot) for v in me.vertices]
        if not pts:
            continue
        mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
        mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
        short, atlas = short_name(o.name, family)
        role, tags = classify(short, family)
        if role == "stern" and min(mx - mn) < 0.15 * max(mx - mn):
            role, tags = "wing", ["fin"]  # Star Sparrow's and Meteor Mantis's tails are fins
        cls = "station" if pack == "ssc" else "capital" if family in CAPITAL_SETS else "fighter"
        tags = [pack, cls] + tags
        if "mirrored" in words(short):
            tags.append("mirrored")
        sockets = sockets_of(role, tags, pts, mn, mx)
        part = {
            "id": f"{pack}/{family}/{short}",
            "family": family,
            "role": role,
            "tags": sorted(set(tags), key=tags.index),
            "node": o.name.split(".")[0],
            "file": rel,
            "atlas": f"{family}_{atlas}" if atlas else family,
            "tris": len(me.loop_triangles),
            "min": [r(v) for v in mn],
            "max": [r(v) for v in mx],
            "sockets": [{"name": n, "pos": [r(v) for v in p], "normal": nm, "size": [r(v) for v in s]}
                        for n, p, nm, s in sockets],
        }
        if pack == "starsparrow":
            part["asset"] = f"/Game/ThirdParty/StarSparrow/Meshes/SM_StarSparrowModule_{short}"
            part["assetOrigin"] = "pivot"
        elif atlas and short in IMPORTED.get((pack, family, atlas), []):
            part["asset"] = f"/Game/ThirdParty/USC/Meshes/SM_USC_Mod_{family}{short}"
            part["assetOrigin"] = "box"
        parts.append(part)


def write(out, parts, sources):
    counts = {role: sum(1 for p in parts if p["role"] == role) for role in ROLES}
    head = {
        "generator": "tools/index_kit_parts.py (US-T763)",
        "frame": "model frame: nose +Z, up +Y, +X starboard; pack metres; relative to the part's own pivot",
        "sockets": "pos: middle of the part's vertices on that face; normal: outward (the neighbour's side); "
                   "size: the patch's extent along the face's two other axes in x, y, z order",
        "assetOrigin": "pivot: the mesh is about the pivot as indexed; box: about the middle of min..max",
        "roles": ROLES,
        "sources": sources,
        "counts": counts,
    }
    d = lambda v: json.dumps(v, separators=(", ", ": "))
    lines = ["{"]
    for k, v in head.items():
        lines.append(f"  {json.dumps(k)}: {d(v)},")
    lines.append('  "parts": [')
    for i, p in enumerate(parts):
        lines.append("    {")
        fields = [k for k in p if k != "sockets"]
        for k in fields:
            lines.append(f"      {json.dumps(k)}: {d(p[k])},")
        lines.append('      "sockets": [')
        for j, s in enumerate(p["sockets"]):
            lines.append(f"        {d(s)}{',' if j + 1 < len(p['sockets']) else ''}")
        lines.append("      ]")
        lines.append("    }" + ("," if i + 1 < len(parts) else ""))
    lines.append("  ]")
    lines.append("}")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w", newline="\n", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    return counts


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    root, out = args[0], args[1]
    parts, sources, failed = [], [], []
    for folder, pack in PACKS.items():
        base = os.path.join(root, folder)
        if not os.path.isdir(base):
            print(f"KitParts: {folder}: not imported here, skipped")
            continue
        for dp, dn, fn in os.walk(base):
            dn.sort()
            if os.path.basename(dp) == "Pieces":
                dn[:] = []
                continue
            for f in sorted(fn):
                if not (f.lower().endswith(".fbx") and "modules" in f.lower()):
                    continue
                path = os.path.join(dp, f)
                rel = os.path.relpath(path, root).replace("\\", "/")
                family = re.sub(r"(_Main|_Parts)?_?Modules$", "", os.path.splitext(f)[0], flags=re.I)
                before = len(parts)
                try:
                    index_file(path, pack, family, rel, parts)
                except Exception as e:  # one unreadable file must not lose the rest
                    failed.append(f"{rel}: {e}")
                    continue
                sources.append(rel)
                print(f"KitParts: {rel}: {len(parts) - before} part(s)")
    if not parts:
        print("KitParts: FAILURE no kit is imported here (Cpp/SourceArt/ThirdParty); nothing written")
        sys.exit(1)
    parts.sort(key=lambda p: p["id"])
    counts = write(out, parts, sources)
    print(f"KitParts: SUMMARY parts={len(parts)} " + " ".join(f"{k}={v}" for k, v in counts.items()) +
          f" failures={len(failed)} -> {out}")
    for f in failed:
        print(f"KitParts: FAILURE {f}")


main()

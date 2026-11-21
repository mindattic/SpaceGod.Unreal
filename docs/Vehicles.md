# Vehicle data (US-T452)

`Data/Vehicles/Vehicles.json` is the one data file that describes every vehicle the game draws: which model each hull
class, local-traffic craft, station, structure, satellite and the mech is drawn with, the mesh to show it, its engine
ports (the nozzles the flames leave from) and every hardpoint of every hull design (guns, launchers, shields, engines,
bridges...). It is written by the game itself (`sg.ExportVehicles`) from the code that builds the models, so it is
exactly what the game shows, and the game reads it back: **engine ports and hardpoint positions and directions come
from this file**, so editing it moves them in the game.

Its editor is the sibling repo **SpaceGod.Studio** (a C# WPF vehicle viewer and port/hardpoint editor at
`D:\Projects\MindAttic\SpaceGod.Studio`), which loads this file and the meshes it names, draws every engine port and
hardpoint as a handle on the mesh, and writes moved handles back into the same file. This repo never reads Studio.

## Writing it: `sg.ExportVehicles`

```
sg.ExportVehicles [dir] [quit]
```

Writes `<dir>/Vehicles.json` (default `<repo>/Data/Vehicles/`) and the meshes under `<dir>/Meshes/`, then logs the
counts; `quit` exits when done. Offscreen:

```
UnrealEditor.exe Cpp/SpaceGod.uproject /Engine/Maps/Entry -game -RenderOffScreen -nosound -unattended -ExecCmds="sg.ExportVehicles quit" -abslog=R:\Temp\vehicles.log
```

An export **keeps the edits already in the file**: the game exports what it is using, and what it is using is the file
(see "Reading it back"). To regenerate from the code's built-in tables, delete (or rename) the file first, or launch with
`-NoVehicleData`.

The output is pretty-printed (2-space indent, LF line endings, UTF-8 without BOM) with a stable order, so a diff shows
only what changed: vehicles sorted by `id`, `classLayouts` keys sorted ordinally, hardpoints in layout order, engine
ports in model order. Numbers are written in the shortest form that reads back to the exact same value (`0.035`, not
`0.034999999999999996`), so an unedited file reproduces the built-in tables bit for bit.

## The frame (one frame for meshes, ports and hardpoints)

Every position and direction in the file is in the game's **model frame**:

- **units: hull lengths** (the hull is one unit long along Z), centred on the hull's bounding box;
- **+X starboard (right), +Y up (dorsal), +Z forward (the nose)**;
- **left-handed** (Unity's convention, which the game keeps). A right-handed viewer (WPF 3D, glTF) that wants the
  ship to look the way it does in the game, not mirrored, shows a point `[x, y, z]` at `(-x, y, z)` and flips triangle
  winding; or simply plots `[x, y, z]` as given and accepts a mirror image. Either way meshes, ports and hardpoints
  stay registered, because they are all in this one frame.

A vehicle's mesh (`mesh`, an OBJ written by the export) is **already in this frame**: load it and draw it with no
transform, then draw each engine port and each hardpoint of each of the vehicle's `layouts` at its `pos`. To get metres,
multiply by the hull's `lengthMeters` (a design's own length; designs of one class can differ).

Stations and structures use the same axes, one unit being the model's own size (its longest side, or its radius for
station bodies and void stations, as stated in its `note`).

## Top level

```json
{
  "version": 1,
  "generator": "sg.ExportVehicles",
  "frame": { "units": "hullLengths", "right": [1, 0, 0], "up": [0, 1, 0], "forward": [0, 0, 1], "handedness": "left" },
  "vehicles": [ { ... }, ... ],
  "classLayouts": { "<layout id>": { ... }, ... }
}
```

| key | type | read back | meaning |
| --- | --- | --- | --- |
| `version` | int | yes | Format version, `1`. The game ignores a file of any other version (built-in tables, with a warning). |
| `generator` | string | no | What wrote it. |
| `frame` | object | no | The model frame above, for reference. |
| `vehicles` | array | partly | Every model the game draws (below). |
| `classLayouts` | object | partly | Every hull design's hardpoints, by layout id (below). |

Unknown keys are ignored by the game, and are **not** kept by a re-export (`sg.ExportVehicles` writes the file afresh
from what the game is using), so put nothing there that must survive an export.

## `vehicles[]`

```json
{
  "id": "usc:GalacticLeopard31",
  "name": "Galactic Leopard 31",
  "kind": "ship",
  "source": "usc",
  "classes": ["Battleship"],
  "civil": [],
  "structures": [],
  "factions": "navy",
  "layouts": ["ship/Battleship/Battleship"],
  "lengthMeters": 2400,
  "mesh": "Data/Vehicles/Meshes/Kit/usc_GalacticLeopard31.obj",
  "meshCommitted": false,
  "sourceMeshes": ["Cpp/SourceArt/ThirdParty/USC/Pieces/SM_USC_GalacticLeopard31_Main_Bow.fbx", "..."],
  "meshTransform": { "space": "model", "matrix": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1] },
  "bounds": { "min": [-0.21, -0.07, -0.5], "max": [0.21, 0.09, 0.5] },
  "sections": ["Stern", "Bow"],
  "enginePortsEditable": true,
  "enginePorts": [
    { "id": "port0", "pos": [0, -0.0223, -0.4562], "exhaust": [0, 0, -1], "radius": 0.0158, "engine": -1 }
  ],
  "rcs": [
    { "id": "rcs0", "kind": "Rcs", "pos": [0.1, 0.05, 0.4], "exhaust": [0, 1, 0], "width": 0.012 }
  ],
  "note": ""
}
```

| field | type | read back | meaning |
| --- | --- | --- | --- |
| `id` | string | key | **Stable id**, `<source>:<model>`: `usc:GalacticLeopard31`, `starsparrow:Battleship`, `kenney:craft_miner`, `ssc:Combined20`, `procedural:Battleship`, `procedural:structure/Shipyard`, `mech:Mech`. Ids come from model names in the game's own tables, never from list positions, so they survive re-exports. Never change one. |
| `name` | string | no | Display name. |
| `kind` | string | no | `ship` (a hull class), `craft` (local traffic), `fighter` (a carrier's launched craft), `station` (free-flying station body, void station, megastructure), `structure` (orbital structure: factory, habitat, shipyard...), `satellite`, `platform` (defence platform), `mech`, `part` (a hardpoint part, pod, round or tow load). |
| `source` | string | no | `usc` (Ultimate Spaceships Creator), `starsparrow` (Star Sparrow kit), `ssc` (Space Station Creator), `kenney` (Kenney Space Kit), `procedural` (built in code, ShipMeshFactory and friends), `mech` (baked Quaternius mech). |
| `classes` | string[] | no | The `ShipClass` names drawn with it (sorted). |
| `civil` | string[] | no | The `CivilCraft` names (local traffic) drawn with it. |
| `structures` | string[] | no | The `StructureKind` names, station-body names or void-site roles drawn with it. |
| `factions` | string | no | `navy`, `pirate` or `any`: which owners fly it (USC capitals: navies fly Galactic Leopard, pirates Void Whale). |
| `layouts` | string[] | no | The `classLayouts` ids whose hardpoints sit on this mesh (every design of every class in `classes`, or a station's layout). Draw them all, or let the user pick one. |
| `lengthMeters` | number or null | no | The first listed layout's hull length in metres (`null` when it has none). |
| `mesh` | string or null | no | Repo-relative path (forward slashes) of an OBJ in the model frame, written by the export from the very hierarchy the game builds (every piece, both sections, cargo; no hardpoint parts). `null` when the export could not build it (a kit not imported on the exporting machine). |
| `meshCommitted` | bool | no | `true` for meshes committed to the repo (`Data/Vehicles/Meshes/*.obj`: the procedural hulls, the mech, all our own geometry). `false` for third-party kit geometry (`Data/Vehicles/Meshes/Kit/`, git-ignored, licensed art that is never committed): it exists only where the kits are imported and the export has been run. |
| `sourceMeshes` | string[] | no | Repo-relative paths of the source art the model is built from (`Cpp/SourceArt/...` FBX for the kits, git-ignored; the `.uasset` package for Kenney craft). Informational; the OBJ is what to draw. |
| `meshTransform` | object | no | `{ "space": "model", "matrix": [16 numbers, row-major, identity] }`: the OBJ is already in the model frame. Kept so a future mesh in another frame can say how to place it (row-major 4x4, applied to column vectors `[x,y,z,1]`). |
| `bounds` | `{min,max}` | no | The mesh's box in the model frame. Lets Studio draw a box (and frame the camera) when the mesh is missing. |
| `sections` | string[] | no | OBJ object names in the mesh (`o Stern`, `o Bow`, `o Hull`, ...): a warship breaks in two at Z = -0.06. |
| `enginePortsEditable` | bool | no | Whether the game reads this vehicle's `enginePorts` back (true for `usc` ships; the procedural, Star Sparrow and kit station nozzles are computed from their geometry and exported for reference only). |
| `enginePorts` | array | **yes** (when editable) | The nozzles the model itself draws, the flames' and glows' sources (below). |
| `rcs` | array | **no, read-only** | The RCS quads, retros and roll jets the game computes at run time from the hull's plating (Views/HullThrusters, US-T231/US-T433), for display only; editing them does nothing. Empty where the game puts none (stations). |
| `note` | string | no | Anything worth knowing (e.g. "drawn only without the USC pack"). |

### `enginePorts[]`

| field | type | read back | meaning |
| --- | --- | --- | --- |
| `id` | string | no | `port<N>`, its index in this list. Ports have no identity beyond their place in the list; Studio may add, remove or reorder them (renumber the ids on save). |
| `pos` | [x,y,z] | **yes** | Middle of the nozzle's mouth, model frame. |
| `exhaust` | [x,y,z] | **yes** | The way the exhaust leaves (unit vector; the game normalises it). Usually `[0,0,-1]` (aft). |
| `radius` | number | **yes** | The nozzle mouth's radius, hull lengths (> 0). |
| `engine` | int | no | `-1` for a nozzle of the hull itself. (The bells on Engine hardpoints are not listed here: they sit on the hardpoints, see below.) |

On a split warship a port belongs to the bow section when its `pos[2] >= -0.06`, else the stern.

### `rcs[]` (read-only)

`{ "id": "rcs<N>", "kind": "Main"|"Retro"|"Rcs", "pos": [x,y,z], "exhaust": [x,y,z], "width": w }`, model frame: where
each jet sits, the way it fires and how wide its plume is (hull lengths). `Main` entries are the main engines as the
thrusters see them (on the engine ports, or on the Engine hardpoints of a model with no ports).

## `classLayouts`

One entry per hull design, keyed by a stable **layout id**, the game's own hull-frame id:

- `ship/<ShipClass>/<DesignName>`, e.g. `ship/Battleship/Battleship`, `ship/Destroyer/Strike Destroyer`;
- `station/defence-platform`, `station/orbital-station`, `satellite/weapons`, `satellite/sensor`.

```json
"ship/Battleship/Battleship": {
  "class": "Battleship",
  "design": "Battleship",
  "lengthMeters": 2400,
  "vehicles": ["usc:GalacticLeopard31", "usc:VoidWhale30", "starsparrow:Battleship", "procedural:Battleship"],
  "hardpoints": [
    { "id": "hp0", "index": 0, "kind": "BoardingPods", "name": "Boarding pods", "pos": [0, -0.045, -0.3],
      "facesAft": false, "coneAxis": null, "coneHalfDeg": null, "buried": false, "spinal": false, "derived": false,
      "socket": "boarding-0", "mount": "Boarding", "size": "Large", "artKey": "BoardingMount/Large" },
    { "id": "hp1", "index": 1, "kind": "HeavyLaser", "name": "Heavy laser", "pos": [0, 0.045, 0.2],
      "facesAft": false, "coneAxis": [0, 0, 1], "coneHalfDeg": 60, "...": "..." }
  ]
}
```

| field | type | read back | meaning |
| --- | --- | --- | --- |
| `class` | string or null | no | The `ShipClass` (null for stations and satellites). |
| `design` | string | no | The design's name (`CombatSpec::DesignName`). |
| `lengthMeters` | number | no | The design's hull length in metres (`LengthKm * 1000`). |
| `vehicles` | string[] | no | The vehicle ids this layout is drawn on (the inverse of `vehicles[].layouts`). |
| `hardpoints` | array | partly | Every hardpoint, in layout order. |

### `hardpoints[]`

| field | type | read back | meaning |
| --- | --- | --- | --- |
| `id` | string | key | `hp<index>`. **Stable**: the index into the design's hardpoint list as the code declares it. |
| `index` | int | key | The same index as a number. The game matches an entry to its hardpoint by `index` **and** `kind`; an entry whose kind does not match the code's is ignored (the code changed under the file). Studio must not add, remove or reorder hardpoints, or change `kind`: what a design carries is game design, not placement. |
| `kind` | string | key | `SubsystemKind` name (`HeavyLaser`, `Engine`, `ShieldGenerator`, `Bridge`, `Radiator`...). |
| `name` | string | no | Display name. |
| `pos` | [x,y,z] | **yes** | Where it sits, model frame (hull lengths, nose +Z, up +Y). An Engine hardpoint's thruster bell (USC capitals) is built here, and its flame leaves from the bell's mouth just aft of it. |
| `facesAft` | bool | **yes** | A turret that rests pointing aft. |
| `coneAxis` | [x,y,z] or null | **yes** | The direction its fire cone is centred on, model frame, or `null` for the mount's own default (along the keel for a fixed mount; the hull's normal at the mount). Write a unit vector: the game takes it as given (so an unedited file stays bit-exact). |
| `coneHalfDeg` | number or null | **yes** | Half-angle of its fire cone, degrees, or `null` for the fitted module's own. 180 bears everywhere. |
| `buried` | bool | no | Buried at the hull's centre (a warship's bridge): nothing of it shows outside. |
| `spinal` | bool | no | A spinal mount, laid down the keel. |
| `derived` | bool | no | `true` for hardpoints the game adds from the hull's size and the rules (a big ship's radiator panels, `Radiators::Fit`): read-only, recomputed from the code, editing them does nothing. |
| `socket` | string or null | no | The socket name a model library binds a part to (`energy-0`, `missile-1`...), null for a hardpoint that is not a fitting slot. |
| `mount` | string or null | no | The slot's mount type (`Energy`, `Ballistic`, `Missile`, `Shield`, `Engine`...). |
| `size` | string or null | no | `Small`, `Medium` or `Large`: the biggest part it takes. |
| `artKey` | string or null | no | The key the model library binds the stock part by, e.g. `TorpedoTube/Large`. |

**Placing hardpoints on a mesh:** hardpoints are in the same model frame as the mesh, so a hardpoint of layout L sits
at `pos` on every vehicle whose `layouts` contains L, with no transform. (The game itself does exactly this: the kits'
and procedural hulls are all fitted to the one-unit frame, and the hardpoints are placed in it.)

## Reading it back (what the game uses)

At start the game loads the file once, from the first of:

1. `SPACEGOD_VEHICLES` (environment variable: the path to a `Vehicles.json`);
2. `<package>/Vehicles/Vehicles.json` (a packaged game: `tools/package.ps1` stages the repo's file there);
3. `<repo>/Data/Vehicles/Vehicles.json` (a development build: `Cpp/../Data/Vehicles/`).

`-NoVehicleData` on the command line skips it. With no file, a file that does not parse, or `version` not 1, the game
uses its built-in tables (UscPorts.inl, the CombatSpecs/Construction layouts) and logs why. The bench
(`tools/bench.ps1`) passes the repo's file to the standalone Core (`--vehicles`), so `Bench 5/5 IDENTICAL` proves the
committed file reproduces the built-in tables exactly.

What it reads, everything else being informational:

- `vehicles[].enginePorts` (`pos`, `exhaust`, `radius`) of vehicles with `enginePortsEditable` (`usc:*`): the whole
  list replaces that model's row of UscPorts.inl. An empty list means "no nozzles"; a vehicle missing from the file
  keeps its built-in row.
- `classLayouts[id].hardpoints[]` (`pos`, `facesAft`, `coneAxis`, `coneHalfDeg`) of non-`derived` hardpoints, matched by
  layout id, `index` and `kind`. A layout or hardpoint missing from the file keeps its built-in values. Hardpoints
  drive the fight as well as the art (where a gun fires from, what a shot hits, fire cones), so a moved hardpoint
  changes the simulation: the bench then (rightly) differs from `Bench/expected-digests.json`.

Values that are not finite numbers, a zero exhaust or cone axis, or a radius not above 0 are ignored (built-in kept)
with a warning in the log (`VehicleData:`).

**Co-op and the bench.** Since hardpoints are simulation data, every peer must run on the same file. The game hashes
the loaded file's bytes (FNV-1a, 16 hex digits, or `builtin` with none); a co-op client sends its hash with its hello
and the host refuses a client whose hash differs from its own ("refused vehicle data differs"). The bench prints the
hash it ran on (`vehicle data: ... hash ...`) on stderr.

`sg.VehicleData` logs where the file was loaded from, how many vehicles and layouts it overrode, and any entry it
ignored.

## Typical Studio round trip

1. Run `sg.ExportVehicles quit` (above) once on a machine with the kits imported, so every mesh exists.
2. Studio opens `Data/Vehicles/Vehicles.json`, lists `vehicles`, loads `mesh` (OBJ, model frame), and draws
   `enginePorts` (sphere + arrow along `exhaust`, `radius` sized) and the hardpoints of each id in `layouts` (a handle
   at `pos`, an arrow along `coneAxis` when set, a cone of `coneHalfDeg`).
3. The user drags handles; Studio writes `pos` / `exhaust` / `radius` / `coneAxis` / `coneHalfDeg` / `facesAft` back
   in place, keeping every other field and the order (and so the diff small). Keep numbers short (round to 4 to 6
   decimals; the game takes any precision).
4. Start the game: the moved ports and hardpoints are where Studio put them. `sg.Thrusters shot <dir>` or the showcase
   shows them; commit `Data/Vehicles/Vehicles.json`.

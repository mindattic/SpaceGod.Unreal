# True scale: audit and plan (US-T713)

The goal: a 1.8 m soldier, a 16 m fighter and a 3.2 km carrier drawn side by side at their real proportions (the fighter
1/200 of the carrier, the soldier 1/9 of the fighter), a cockpit drawn at 1 Unreal unit = 1 cm with no jitter, and nothing
drawn that cannot be seen, while the simulation keeps every hull's state whether it is on screen or not. This page
records what is drawn today, where the simulation reads hull lengths, and the design the follow-on tasks build:
US-T715 (render-side real hull lengths) and US-T716 (floating origin round the seat).

## 1. How a length becomes pixels today

Three conversions, all linear for hardware:

1. **Catalog length** (`CombatSpec::LengthKm`, `Combat/CombatSpecs.cpp`, one per design; `Ship::LengthKm()` returns it).
2. **Scene units** (`ScaleModel::ShipLength`, `Cpp/Source/SpaceGodCore/ScaleModel.cpp`):
   `LogLerp(km / 1000, GameShipScale (7) x seatZoom x km, blend)`. Free view at gameplay blend: 7 units per km.
   `seatZoom = clamp((InhabitedSizeKm / 4) ^ 0.4, 0.04, 1)` (`ScaleModel::SeatZoomFor`, US-T434).
   The battle space uses the same map: `UnitySpaceApp::BattleKmScale() = ShipLength(1.0)`.
3. **Unreal units** (`UnrealSpace::ToUnreal`, `Cpp/Source/SpaceGod/UnrealSpace.h`): `CmPerUnit = 100` UU per scene unit.

So at gameplay blend a hull of L km is `700 x zoom x L` cm in Unreal.

## 2. Real lengths against the catalog

"Real" is the length US-T715 draws. Capital and mid-size hulls are already plausible and keep their catalog length;
only the strike craft, small boats and pickets are too long (the catalog fighter is 100 m, 6x a real one and 55 persons
long). Strike-craft numbers follow the wing craft the battles already fly at true size (`FighterAI::FighterLengthKm`
0.016 and `BomberLengthKm` 0.024, `Combat/Fighters.h`).

Columns: catalog km (sim); real m (drawn after US-T715); drawn today in the free view (gameplay units / Unreal cm);
drawn after US-T715 in the free view; seat zoom today (catalog length) and after (real length) when that hull is held.

| ShipClass | Catalog km | Real m | Today units / cm | Real units / cm | Zoom today | Zoom real |
|---|---|---|---|---|---|---|
| Carrier | 3.2 | 3200 | 22.40 / 2240 | 22.40 / 2240 | 0.915 | 0.915 |
| Battleship | 2.4 | 2400 | 16.80 / 1680 | 16.80 / 1680 | 0.815 | 0.815 |
| GateBreaker | 2.0 | 2000 | 14.00 / 1400 | 14.00 / 1400 | 0.758 | 0.758 |
| CommandCruiser | 1.6 | 1600 | 11.20 / 1120 | 11.20 / 1120 | 0.693 | 0.693 |
| Defence platform (not a class) | 1.4 | 1400 | 9.80 / 980 | 9.80 / 980 | 0.657 | 0.657 |
| Cruiser | 1.2 | 1200 | 8.40 / 840 | 8.40 / 840 | 0.618 | 0.618 |
| Freighter | 0.6 | 600 | 4.20 / 420 | 4.20 / 420 | 0.468 | 0.468 |
| Destroyer | 0.45 | 450 | 3.15 / 315 | 3.15 / 315 | 0.417 | 0.417 |
| Frigate | 0.32 | 320 | 2.24 / 224 | 2.24 / 224 | 0.364 | 0.364 |
| Miner (ore hauler) | 0.3 | 300 | 2.10 / 210 | 2.10 / 210 | 0.355 | 0.355 |
| LaneTender | 0.26 | 260 | 1.82 / 182 | 1.82 / 182 | 0.335 | 0.335 |
| Explorer | 0.22 | 220 | 1.54 / 154 | 1.54 / 154 | 0.313 | 0.313 |
| Corvette | 0.22 | 220 | 1.54 / 154 | 1.54 / 154 | 0.313 | 0.313 |
| OreTug | 0.2 | 200 | 1.40 / 140 | 1.40 / 140 | 0.302 | 0.302 |
| GatePicket | 0.12 | 60 | 0.84 / 84 | 0.42 / 42 | 0.246 | 0.186 |
| CaptureTug | 0.12 | 45 | 0.84 / 84 | 0.32 / 32 | 0.246 | 0.166 |
| Gunship (heavy fighter) | 0.14 | 40 | 0.98 / 98 | 0.28 / 28 | 0.262 | 0.158 |
| Courier | 0.08 | 40 | 0.56 / 56 | 0.28 / 28 | 0.209 | 0.158 |
| AssaultShuttle | 0.12 | 30 | 0.84 / 84 | 0.21 / 21 | 0.246 | 0.141 |
| Bomber | 0.14 | 22 | 0.98 / 98 | 0.154 / 15.4 | 0.262 | 0.125 |
| Fighter | 0.1 | 16 | 0.70 / 70 | 0.112 / 11.2 | 0.229 | 0.110 |
| Recon | 0.08 | 14 | 0.56 / 56 | 0.098 / 9.8 | 0.209 | 0.104 |
| Interceptor | 0.07 | 12 | 0.49 / 49 | 0.084 / 8.4 | 0.198 | 0.098 |

Not hulls (unchanged, already true; listed for proportion):

| Thing | Length m | Source | Free-view units / cm | Seat zoom |
|---|---|---|---|---|
| Person (EVA worker, ejected pilot) | 1.8 | `InhabitedSizes::PersonKm` (`Embodiment/Embodiment.h`) | 0.0126 / 1.26 | 0.046 |
| Mech | 7.2 (height) | `MechWalker::HeightKm` | 0.050 / 5.0 | 0.080 |
| Satellite | 8 | `InhabitedSizes::SatelliteKm` | 0.056 / 5.6 | 0.083 |
| EVA torpedo | 9 | `EvaPilot::TorpedoLengthKm` | 0.063 / 6.3 | - |
| Launched fighter (wing craft) | 16 | `FighterAI::FighterLengthKm` | 0.112 / 11.2 | 0.110 |
| Launched bomber (wing craft) | 24 | `FighterAI::BomberLengthKm` | 0.168 / 16.8 | 0.129 |
| Cargo pod | 14 | proportion sheet | 0.098 / 9.8 | - |
| Breacher round | 60 (real 18) | `BreacherBays` | 0.42 / 42 | - |

Ratios after US-T715: carrier / fighter = 200, carrier / person = 1778, fighter / person = 8.9, carrier / interceptor = 267.
Today a Fighter *hull* (`ShipClass::Fighter`, 100 m) and a *launched* fighter (wing craft, 16 m) of the same role are
drawn 6x apart; after US-T715 both are 16 m.

What the seat does to the drawn size (gameplay blend, cm in Unreal):

| Seat held | Zoom | Held hull | Carrier beside it |
|---|---|---|---|
| Fighter helm today (100 m) | 0.229 | 16 cm | 513 cm |
| Fighter helm, real (16 m) | 0.110 | 1.23 cm | 246 cm |
| Person on the plating | 0.046 | 0.058 cm | 103 cm |

That last table is why US-T716 exists: a real-length cockpit is about one centimetre of Unreal space, so the near plane
(`GNearClippingPlane`, floored at 0.01 cm in `AUnitySpaceAppActor::ApplyCamera`) is a large fraction of the cockpit,
and every shadow, light radius and depth bias authored in centimetres is a thousand times too coarse for it.

## 3. What the simulation reads from catalog lengths

`CombatSpec::LengthKm` is simulation data, not a drawing hint. Changing a catalog length changes outcomes and the
saved format, so the Bench digests (`Bench/expected-digests.json`) would move. Readers in `SpaceGodCore`:

- **Separation and collision**: `Fleet::SafeDistanceKm` (`3 + 4 x (La + Lb)`), the true-size separation solve
  (`Fleet.cpp`, body radius `0.5 x LengthKm x ShipLod::ExtentFactor`, weight = length), `SectionColliders` capsules,
  `Collisions`, station cores (`Berths::SolidKm`).
- **Battle layout**: `Battle::NewUnit` takes `ship->LengthKm()` (`Battle.cpp:293`); a unit's local frame is scaled by
  its length (`Battle.cpp:230`); platforms use `CombatSpec::PlatformLengthKm`.
- **Class tiers by length**: `Choreography` (capital and light thresholds on `u->LengthKm`), `Formations`
  (`Slot(..., leadLengthKm)`; `Fleet.cpp:310` reach in lead lengths), `FlightDecks` (deck half-length in ship lengths).
- **Handling**: `CombatSpec::PitchYawInertia` and `RollInertia` (mass x length squared), so turn response.
- **Wrecks, salvage, boarding, broadsides, dogfighting, alpha strikes, self-destruct, mech and EVA** read unit or
  hull lengths for reach, blast radius and approach points.
- **Persistence**: `LengthKm` is saved on `CombatSpec`, battle units and wrecks (`Persistence/Registrations/Combat.cpp`).

View-only readers in `SpaceGodCore`, which do not reach `Fleet::StateOf` and so are digest-safe to change:

- `Fleet`'s drawn separation solve (`Fleet.cpp`, the second solve after "The view": `drawnHalf` from
  `ScaleModel::GameplayShipLength(s->LengthKm(), DrawnSeatZoom)` at `DrawnBlend`), stored in `drawnSeparation` and
  only added by `DrawnOffsetOf`.
- `IEmbodiable::InhabitedSizeKm` and `ScaleModel::SeatZoom` ("drawing only: nothing in the simulation reads it").
- `RenderLod` / `ShipLod` tiers, by apparent pixels.

**Decision**: real lengths are a render-side table. The catalog, masses, handling and every sim reader stay as they
are; `tools/bench.ps1` keeps matching. Fighters keep flying their km/s numbers, so a 16 m fighter now covers 6x more
of its own lengths per second on screen than the 100 m drawing did: the speed reads as a fighter's.

## 4. Where scene positions become floats

The floating origin already exists, rebasing every frame rather than past a threshold:

- Every position is a double `DVec3` in scene units, Sun at the origin (`ScaleModel::Layout`, `MapNear`,
  `TransitScene`, `LaneScene`).
- `UnitySpaceApp::Relative(DVec3)` (`UnitySpaceApp.Main2.cpp`) subtracts `SpaceCamera->ScenePosition` in double and
  only then casts to the float `UnityCompat::Vector3` (about 80 call sites). `ToVector` (same file) casts directions and
  camera-relative offsets, the same way (`UnitySpaceApp.Embodiment.cpp:328` subtracts the camera first).
  `Camera/SpaceCamera.h` and `Camera/StarTravel.h` have their own `ToVector` for poses.
- `Transform::localPosition` (`UnityCompat/UnityRuntime.h`) is float, camera-relative. `UnrealSpace::ToUnreal`
  multiplies by `CmPerUnit` into an LWC `FVector` when it reaches a scene component. The Unreal camera only ever turns
  at the origin (`AUnitySpaceAppActor::ApplyCamera`).

So float precision near the camera is already relative to the camera. A float position 2.5 units out (a carrier's
length at fighter zoom) is exact to about 1e-7 units. The jitter and clipping a true-scale cockpit would show do not
come from that cast. They come from **absolute Unreal size**: a 1.2 cm cockpit against a near plane floored at
0.01 cm, Lumen and shadows tuned in centimetres, and `UpdateClipPlanes` putting the near plane at a quarter of the
nearest surface. US-T716 has to check, and keep, the rule that nothing casts an absolute scene position to float
before the camera is subtracted. The paths to audit are `Smoke/*`, `GodInsideCommand.cpp`, `EffectsCommand.cpp` and
`SeatScaleCommand.cpp`, which carry their own `V()`/`ToV3()` helpers.

## 5. The plan: near field and far field

Three parts. Each is image-invariant except the hull lengths, so the views change only where intended.

### 5.1 Real hull lengths, render side (US-T715)

- A table `TrueLengthM(ShipClass)` beside `ScaleModel` (the "Real m" column above). Where the real length equals the
  catalog length, the entry returns it.
- A `ScaleModel` setting `TrueScale` (default on). The drawn length of a hull is `ShipLength(DrawnLengthKm(ship))`,
  where `DrawnLengthKm` is the table's length when `TrueScale` is on, else `ship->LengthKm()`. **`ShipLength(km)` itself
  does not change**, so `BattleKmScale()`, the altitude mapping and every structure, gate and wreck keep their scale.
  Only per-hull drawn lengths shrink.
- Callers to switch: the main ship render (`UnitySpaceApp.Main2.cpp` `shipLength[i]`, wrecks, unit radius),
  `FighterViews` for `UnitKind::Ship` units (wing craft already pass 16/24 m), `BattleFx`, `GraveyardView`,
  `TourStops`, `UnitySpaceApp.Possession/Streaming/Effects`, and `Fleet`'s drawn separation solve. That solve must use
  the same drawn length, or small craft keep the spacing of 100 m hulls. Hardpoint, turret and engine positions are
  fractions of length, so they follow.
- Seat zoom: `UnitySpaceApp.Main2.cpp:950` (`SeatZoomFor(InhabitedSizeKm())`) uses the held hull's drawn length, so the
  fighter helm zooms at 0.110, not 0.229.
- Render limits follow on their own: `RenderLod` tiers are by apparent pixels (`RenderLod::ApparentPx` of the drawn
  length), so a 16 m fighter becomes an impostor or is culled at 1/6 of today's distance, and full models are drawn only
  where they cover pixels.
- `sg.Scale` prints this table from the code (class, catalog km, real m, drawn units at the current blend and zoom).

### 5.2 Render scale round the seat (US-T716)

The floating origin is already per frame (section 4). What a true-scale cockpit needs is **Unreal units per scene unit
that follow the seat**, so the near field is drawn at 1 UU = 1 cm of true size:

    RenderCmPerUnit = 100 000 / ShipLength(1.0)        // cm per scene unit: 1 true km of hardware = 1e5 UU

At gameplay blend that is `100 000 / (7 x zoom)`: 14 286 in the free view, 130 000 at a fighter's helm, 310 000 for a
person, against today's fixed 100. Scaling every camera-relative position and size by one factor about the eye changes
nothing on screen: angles and depth order are the same. So this is one runtime value in place of the `constexpr
CmPerUnit` (`UnrealSpace.h`, 12 uses). It is eased with the seat zoom (it moves with `ShipLength(1.0)`, which already
eases in log space over 0.6 s). It costs one multiply per conversion. With it:

- Cockpit near plane 5 to 10 UU (5 to 10 true cm). `UpdateClipPlanes` converts `ClipDistance` with the same factor,
  and the 0.01 cm floor in `ApplyCamera` becomes 5 cm while a seat is held.
- Everything authored in cm scales with it: light attenuation radii (`Rendering/HullLighting.cpp`), shadow and Lumen
  distances, and any Niagara sizes. Those are audited in the same task. Audio already works in scene units.
- `FromUnreal` (mouse picks, imported-mesh bounds in `UnityRendering.cpp` and `UnityScene.cpp`) divides by the same
  factor.
- `sg.Origin` reports the camera's scene position (the origin), `RenderCmPerUnit` and the near plane.

### 5.3 The far field

Multiplying by `RenderCmPerUnit` pushes far objects out. In the free view Neptune's 4.5e6 units become 6.4e10 cm, inside
Unreal's large-world limit (`UE_LARGE_WORLD_MAX`, about 8.8e12 cm). At the realistic blend (`ShipLength(1) = 0.001`,
factor 1e8) they would not be. So past `FarFieldCm` (a setting, default 1e11 cm = 1 million km), a position is drawn
log-compressed along its own direction, and its size is shrunk by the same ratio:

    d' = FarFieldCm x (1 + ln(d / FarFieldCm)),   scale' = scale x d' / d

Angular size and the depth order between far objects are kept. Only depth inside one far object is squashed, which no
one can see at that range. The work happens in `ToUnreal` for positions and in the view's scale for sizes. It is a
branch and a log per far object per frame, and only worlds, orbits and lane curves ever reach it.

### 5.4 What is drawn, what is simulated

- **Near field**: hulls within `NearFieldKm` (a setting, default 50 km true) of the seat or camera are drawn as full or
  simplified models at real length, by `RenderLod`'s pixel tiers.
- **Beyond**: impostor, icon, aggregate or culled, as `RenderLod` already decides. Real lengths make small craft fall
  through those tiers sooner, so a 100-ship launch draws its far wings as impostors and marks, not 100 models.
- **Simulation**: unchanged and independent of what is drawn. Every star system is stepped on the shared clock; only
  the active one has views (`UnitySpaceApp` class comment). Nothing in 5.1 to 5.3 is read by the simulation or written
  into the lockstep frames, and each co-op peer draws round its own seat.

### 5.5 Launch tubes (US-T843, US-T854)

A true-scale carrier launches a whole wing at once the way the Galactica does: through rows of launch tubes, each a
true-size bore (`FlightDecks::TubeWidthKm` 18 m by `TubeHeightKm` 12 m, `TubeWallKm` 6 m of hull between bores, at most
`TubesPerTier` 10 to a tier) running in from its mouth 0.15 ship lengths, at most 200 m. A tube is a launch-only
`FlightDeck` (`Tube >= 0`): craft are fired out of its mouth and never land in it, so recovery stays on the flight decks.

Where a hull's tubes come from:

- **Its design.** `SubsystemDef::LaunchTubes` on a hangar with a mouth (`MouthAxis`) lays that many tubes in tiers across
  the mouth, as many to a tier as its width holds. An assembled hull (US-T765) gets it from `VehicleData::FromDesign`:
  a hangar section's kit sockets named `tube*` count as its tubes (and are never picked as its mouth); a part with none
  takes the design's opt-in count, `FactionStyle::LaunchTubes` (`launchTubes` in a Data/FactionStyles.json scale style,
  `tubes=<n>` on `sg.Assemble`), copied to `ModularDesign::LaunchTubes`. `VehicleData::ApplyGeometry` gives the count to
  the first Hangar placed on that mouth. Every built-in design and style has none, so the Bench is unchanged.
- **The setting.** A carrier whose design has none gets `dogfight.launchTubes` (default 0, up to 120) in two flight pods
  off its flanks, half each side.

The flight deck view draws a tube by its mouth alone (no breech frame), so dozens of small mouths read as rows of
ports and not as a lattice of nested frames. How a tube fires its craft is in docs/FlightFeel.md.

## Settings this plan introduces

| Setting | Default | Where |
|---|---|---|
| `TrueScale` (real hull lengths) | on | `ScaleModel` (US-T715) |
| `TrueLengthM(ShipClass)` | the "Real m" column | beside `ScaleModel` (US-T715) |
| Render cm per true km of hardware | 100 000 | `sg.Origin.CmPerTrueKm`, `UnrealSpace` (US-T716) |
| Cockpit near plane | 5 cm | `sg.Origin.CockpitNearCm`, `UnitySpaceApp::UpdateClipPlanes` (US-T716) |
| `FarFieldCm` | 1e11 | `sg.Origin.FarFieldCm`, `UnrealSpace` (US-T716) |
| `NearFieldKm` | 50 | render side (US-T715/US-T716) |
| Launch tubes per carrier without its own | 0 (none) | `dogfight.launchTubes`, `FlightDecks::LaunchTubes` (US-T843) |

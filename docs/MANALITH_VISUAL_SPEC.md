# Manalith visual spec (shared: Unity.GridGame and SpaceGod)

One spec for the Manalith / Midnight Geode satellite, so GridGame (Unity, GG-T679) and SpaceGod (Unreal, US-T458)
draw the same thing. This file is meant to be identical in both repos (GG-T680). Change a value here and in the
other repo in the same breath.

Reference frame: `docs/images/manalith-unity-00.png` (Unity, t = 20 s, black background). SpaceGod's matching
frame goes beside it.

## Units and frames

- **D** = the satellite's longest bounding-box edge, as the mesh is imported, measured at its rest pose. Every
  distance below is in D, so the spec holds at any scale. GridGame scales the model so D = 1 unit.
- The mesh is centred on its bounding-box centre. All rotation and emission happen about that centre.
- **View space**, used for light directions: +x right, +y up, +z toward the viewer (right-handed, OpenGL-style).
  Unity converts to world space with z negated, because its camera looks down +z. Unreal converts to its own axes.
- Angles are degrees. Times are seconds.

## Source asset

- Meshy "Midnight Geode" (2026-11-22): the FBX plus base colour, metallic, roughness and normal PNGs (2048²).
- The source mesh has 1,218,020 polygons and is about 1.901 x 1.164 x 1.162 in Blender units. GridGame ships a
  decimated copy: 40,000 triangles (Blender Decimate, collapse), with the textures at 1024².
  - Unity: `Resources/Manalith/manalith-satellite-00.fbx` and `-00.png` (base), `-normal-00.png`, and
    `-metallic-00.png`. The last one is packed: R = metallic, A = smoothness = 1 − roughness.

## Tumble (closed form, so it is the same at any frame rate)

```
pose(t) = Euler(pitch 15, yaw 0, roll 25)                  // start pose
        * AngleAxis(|w| * t, w / |w|)                       // w = (3.0, 7.0, 1.7) deg/s about local X, Y, Z
        * Euler(x = 4 * sin(2*pi*t / 40), 0, 0)            // precession wobble, ±4° on X over 40 s
```

- A constant angular velocity in the body frame. This is exactly what "rotate by (3, 7, 1.7)·dt in local space
  every frame" converges to. |w| = 7.8 deg/s.
- Unity's `Quaternion.Euler(15, 0, 25)` applies Z, then X, then Y. Unreal's `FRotator(pitch, yaw, roll)` has its
  own order and its own axis signs, so the start pose is approximate across engines. The rates and the wobble
  carry over exactly.

## Material

- PBR with the Meshy maps as-is: base colour (sRGB), metallic and roughness (linear), and the normal map at
  strength 1.0.
- A Fresnel rim adds emission: `MANA_BLUE * 1.5 * pow(1 − saturate(dot(V, N)), 3)`, with N taken after the
  normal map. Nothing else is emissive.
- **MANA_BLUE = #7590C8** (sRGB; linear ≈ 0.178, 0.279, 0.578). It is GridGame's Manapunk theme accent, "the
  Manalith's blue", sampled from the Manalith art (GG-T678).

## Debris

| Item | Value |
|---|---|
| Steady rate | 12 shards/s |
| Spawn | random points on a sphere of radius 0.45 D about the centre (the shell only) |
| Shard size | 0.006 to 0.025 D across, uniform random |
| Start velocity | outward (radial) at 0.02 D/s ±50% (0.01 to 0.03) |
| Fall | 0.05 D/s², constant. GridGame: screen-down. SpaceGod: toward the planet |
| Spin | 30 to 180 deg/s about a random axis |
| Life | 6 s, with alpha 1 until 4.5 s, then linear to 0 at 6 s |
| Dark shards (80%) | albedo = the base texture's average colour × 0.5 = **#342E42**, lit, no emission |
| Glowing shards (20%) | the same albedo plus MANA_BLUE emission × 2 |
| Chunk bursts | 5 chunks of 0.03 to 0.05 D, dark, every 8 to 12 s (uniform). The same motion rules apply |
| Shape | irregular low-poly rocks: icosahedra with each vertex radius jittered ×0.75 to 1.15 and the rock squashed (1, 0.6 to 1, 0.7 to 1). 4 variants |
| Seed | 1337 for every random stream |
| Simulation space | world: shards don't follow the tumble once they leave |

Burst times are drawn up front from the seed: first at U(8, 12), then each next one U(8, 12) later.

## Lighting (where the engine controls it)

- Key: a white directional light at intensity 1, from view-space direction (-0.5, 0.7, 0.5), i.e. upper left,
  toward the viewer.
- Rim/back: a MANA_BLUE directional light at intensity 0.4, from view-space (0.4, 0.3, -1), i.e. behind the
  satellite and to the upper right. The original spec gave no direction for it, so this one is chosen here.
- Bloom: threshold 1.0, intensity 1.
- Ambient is whatever the scene has. In GridGame it is Unity's default ambient (not set by the rig).

## Framing (UI)

- The camera has a 30° vertical FOV and sits 1 / 0.7 / (2·tan 15°) = 2.666 D from the centre, so D spans 70% of
  the frame height.
- GridGame: a square 768² frame. The backdrop scales the frame by 1/0.7, so the satellite keeps the on-screen size
  of the old flat art. Shards fade in the last 12% toward each frame edge so they never cut off hard (GridGame
  only, since it frames the satellite in a box).

## Reference frame

- t = 20 s, reached in fixed 1/30 s steps from t = 0, seed 1337, on a black background.
- Unity at that moment: 72 shards alive (12/s × 6 s, the steady state) and 5 chunks, from the burst at
  t ≈ 17.33 s. The bursts come at t ≈ 8.87 s and 17.33 s: C#'s `System.Random(1337)` draws the burst times,
  which are 8.85 + 8.46.
- Each engine's own RNG decides the positions, sizes and spins, so expect the same counts and the same look, not
  the same pixels. A port that wants the same burst times can copy the two numbers above.
- Unity re-renders it with `tools/unity.ps1 -Method GridGame.EditorTools.GaidenBuild.ManalithShot` (needs
  graphics, not `-nographics`). The log prints the burst times and the live counts.

## SpaceGod (Unreal) mapping (US-T458, US-T460)

This section is SpaceGod's own. Everything above is a verbatim copy of Unity.GridGame's file (GG-T680), the source of
truth. The constants live in `Cpp/Source/SpaceGod/Views/ManalithView.h`.

- **Where:** the Manalith orbits Wendmere in Nightglass, a system 6.4 ly from Sol that only FTL reaches and that is
  for observation only (`SpaceGodCore/Systems/ManalithSystem.*`). Its body is a `Station` 3 km across (D = 3 km), on
  a 21,000 km orbit inclined 28°. `FacesParent` is set, so its prime meridian points at Wendmere, and the view takes
  that as "down".
- **Assets:** `tools/import-manalith.ps1` imports into `/Game/SpaceGod/Manalith/`. Blender decimates the mesh to 60k
  faces and centres it on its bounding box, and the maps are capped at 2048. That gives `manalith-satellite-00`,
  `manalith-{base,metallic,roughness,normal}-00`, `M_ManalithLit` and `M_ManalithShard`. Unreal keeps metallic and
  roughness as separate linear masks; it does not pack smoothness.
- **Units:** the view's root is scaled to the drawn radius, so D = 2 local units. Shards live in D under that root,
  so the scale toggle moves them all together and none pops.
- **Tumble:** the same closed form, written in the game's Unity-convention compat layer
  (`Quaternion::Euler(15, 0, 25) * AngleAxis(|w|t, w/|w|) * AngleAxis(4 sin(2πt/40), X)`). The compat layer converts
  it to Unreal's axes, so the start pose matches Unity's Z-X-Y Euler order exactly, not approximately. It runs on
  unscaled real time, on top of the orbit.
- **Normal map:** the Meshy map is OpenGL-style (green up). Unity takes it as-is. Unreal flips green
  (`FlipGreen = -1`).
- **Rim:** the Fresnel rim is as specified. The rim/back light is not a separate light: Unreal adds it in the
  material as `MANA_BLUE * 0.4 * pow(1 − N·V, 4)`. Every emission goes through the game's exposure (`SG_Expose3`),
  as all its hull emissives do.
- **Key light:** the system's star, as a real shadowing directional light (the lit-hull path, US-T172), plus the
  sky light. It is not the fixed view-space key of the reference frame.
- **Shards:** pooled (100), with the Unity rock recipe and the same rates, sizes, life and fade. They fade through a
  screen-door dither (`Fade` 0..1 per shard), because opaque Default Lit has no alpha. They fall toward Wendmere.
  "World" space here means they do not follow the tumble; they do still ride the orbit with the satellite, or at
  orbital speed they would be left behind at once. The random stream is a xorshift seeded 1337, not C#'s
  `System.Random`, so counts and look match, not positions or burst times.
- **Bloom:** the game's own grade, not the reference rig's threshold 1 / intensity 1.
- **Reference frame:** `docs/images/manalith-unreal-00.png`, shot by `sg.Manalith shot <file> quit` (offscreen works,
  e.g. `UnrealEditor.exe Cpp/SpaceGod.uproject /Engine/Maps/Entry -game -RenderOffScreen -ExecCmds="sg.Manalith shot
  <abs path> quit"`). It is taken 20 s after the view first draws the Manalith, with the sim clock paused so the orbit
  holds still. Differences from the Unity frame: the game's own field of view (about 60°, eased every frame by pace
  and cruise), with D framed at 70% of the height for it; the game sky behind instead of black; the warm G4 star of
  Nightglass as the key instead of a white view-space key, which makes it read redder than Unity's lavender; and the
  game's grading and bloom. Same model, maps, rim colour, tumble, shard rates and shard colours.

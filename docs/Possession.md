# Possession: the show, and the possessed view (US-T446)

The owner: "the effect of SpaceGod inhabiting/possessing a person/ship/station needs to be an impressive show: an almost
imperceptible energy enters the vehicle and changes what can be seen based on what the pilot sees".

Whenever this machine's player takes a seat of any kind (a ship's helm, a launched craft, a gun, a station, a satellite,
a mech, an EVA repair worker, an ejected pilot) a faint energy crosses from the god's eye to it and seeps into the hull,
and what can be seen becomes what that pilot perceives. Letting go, the energy comes back out and the god's full view
returns. It is view only: nothing in the simulation, the sensors' pictures or the co-op digest changes, and each co-op
peer plays its own player's show and sees through its own player's seat.

Every tunable (timings, sizes, brightness, colours, the sensor suites of the seats, the frame's alphas) is in one place:
`Views::PossessionTune` (`Cpp/Source/SpaceGod/Views/PossessionFx.h`).

## How a seat take is seen

`UnitySpaceApp::UpdatePossession` (`UnitySpaceApp.Possession.cpp`), first thing in `UpdateViews`, compares what is held
now (the helm flown here, `IsPiloting()`, else the session seat, `HeldNonHelm()`, compared with `EmbodiableEquals`) with
last frame. A change starts the show: going in when something is now held (at `SwitchSpeed` from one seat straight to
another, as the camera hurries), coming out when nothing is. So every path plays it alike: the keys, the picker, a
co-op frame granting a seat (`CoopSeatTakenHere`), the helm's co-op take, a load.

## The show going in (about 2.7 s, in step with the camera's 1.6 s fly-in)

- **The energy crossing** (`Views/PossessionFx`): twenty-two motes, small soft streaks of pale blue-white light (the hull
  rim's own hue), leave the edge of the god's view one after another (golden-angle spaced round it) and curl in to land
  along the near side of the hull (round the suit, for a person, whose glow is smaller and softer, as the eye is close by). Their path is worked out each frame from the camera to
  the hull as both move, so they keep flowing from the viewer into the vessel all through the fly-in.
- **The seep**: a rim glow just behind the hull (its silhouette's edge lights, peaking about 1 s in), a sheen sliding
  over the plating bow to stern, and the hull model's own rim term (the selection rim, `_Highlight` in `M_Ship`, now a
  float through `ShipView::Apply`) brought up to 2.2 and down again: the light goes into the hull and fades there.
- **The breath**: the field of view narrows by 2.6 degrees and back (a sine squared over 1.7 s, `PossessionFovBreath` in
  `UpdateClipPlanes`), and the screen takes the faintest wash of the energy's colour (5.5% at its peak, `Hud::DrawPossession`).
- **The view changes** from 1.25 s: the possessed view eases in over 0.9 s (`PerceptionShown`, 0 god .. 1 possessed);
  the filtering by the pilot's picture switches over half way.

Letting go plays it backwards (about 2 s): the rim flares briefly and fades, the sheen slides back stern to bow, the
motes leave the hull and drift out towards the eye, thinning away; the view breathes wider; the god's full view is back
at once in what is drawn, and its orbit lines fade back in over 1.5 s.

Reduce flashes and shake / reduced motion (Settings > Accessibility): the motes and the breath are off (the rim and the
glow only fade), and the light is dimmed (`PlayerOptions::FlashScale`, never under 0.35-0.4). A scripted run (the
screenshot commands) still plays the show: it reads only the profile's setting (`UiMotion::ProfileReducesMotion`), not
`UiMotion::Enabled`, which is off in every scripted run.

## The possessed view: what the pilot perceives

`UnitySpaceApp::PerceivedPicture()` is the picture the view is filtered by:

| Held | Seen through | Picture |
|---|---|---|
| a ship's helm | its sensors | the simulation's own, `Sensors::Player` (unchanged) |
| a launched craft, a gun, a satellite, a mech | its (or its hull's) sensors | the seat's view-side picture: the craft's hull's `SensorSuite::Of`, or `CraftRangeKm`; a gun the hull's fire control (a station's gun the station's); a satellite `SatelliteRangeKm`; a mech `MechRangeKm` |
| a station | traffic control's panorama | the seat's view-side picture with `SensorSuite::Station` |
| an EVA worker, an ejected pilot | eyes alone | the seat's view-side picture by eye only |

The seat's view-side picture is a `ContactBook` of the app's own (never the simulation's), swept every half second
(`SweepPerception`): each ship's place and velocity as drawn (its battle unit's in a fight, else its schedule's state)
against the seat's pose. A sensor seat resolves it with `Sensors::Resolve` (its signature, its transponder, or its own
flag's IFF); every seat also sees by eye: within visual range (one arcminute, `VisualRangeKm`) a blip, its class within a
third of that, who it is within 8%. Each track is put where the light it was seen by left it (back along its course by
the delay) and keeps its lag, so its label says "(1.3 s ago, light lag)"; a track not seen goes cold and is held 180
sim seconds as a "?" where it was last seen. No pursuit, fleet cache or sensor state is stepped: only drawn state is read.

What the picture filters:

- **Ship models**: possessed, a hull is drawn only if the picture holds it live, it is the hull held, or it is close
  enough to see by eye (`PerceivesShipModel`). In the god's view everything is drawn as before.
- **Icons, labels, the lost "?" marks**: the HUD's existing helm filtering (U.SPC-T66) now runs for every seat
  (`Hud::Picture`), and a live track's icon and label are drawn light-lagged (`PerceivedLagOffset`, `Hud::ContactScene`).
- **Picking with the mouse**: only what is perceived (or close enough to see).
- **The god's charts**: the orbit lines and the disc fade out (as in a fight) and the lanes hide while possessed.

The view's character on screen (`Hud::DrawPossession`, eased in with the view, shown with the HUD off too):

- **Sensors**: thin corner brackets round the view with a tick at each edge's middle, and the caption "THROUGH ITS
  SENSORS · REACH 400,000 km".
- **Eyes**: a helmet visor's dark rounded rim, and "THROUGH THE VISOR · EYES ONLY".
- **Panorama**: soft bands top and bottom, a bearing scale along the lower one (ticks every 5 degrees, labels every 45),
  and "TRAFFIC CONTROL · PANORAMIC · REACH 600,000 km".

## Fly this: dropping into a battle (US-T806)

The selection card in the god view has a **Fly this** button on a battle's marker (a ship or unit in a fight, a battle's
row in the navigator, a world a fight is under way at) and on a fleet's ship. `UnitySpaceApp::FlyThis`
(`UnitySpaceApp.Embodiment.cpp`) picks the seat and hands it to `TakeSeat`, the one seat-change path, so the show above
and the camera's glide play as for any take; time warp drops to 1x and comes back with P.

- The craft selected, if it is a fighter nobody holds.
- Else the free fighter of the selected unit's side nearest it (any side for a world): flying, held by nobody, not
  on its way home to land, not a boarding shuttle.
- Else a squadron launched from that side's nearest carrier with craft aboard (fighters first, then interceptors, heavy
  fighters, bombers, recon), through `HelmAbilities::Use` as its helm would, out through its flight decks and launch
  tubes; the player takes its lead, so in a tube the cockpit looks down it from the breech (US-T786). Single player only:
  co-op has no command to carry a launch ordered from the god view.
- Else any side's free fighter. Out of a fight, the ship's own helm.

## The consciousness between bodies: the seat transition (US-T855)

The owner: "need transition for god-to-ship, and ship-to-ship, and ship-to-god; the universe needs to slow to a
standstill, drift the view like a ghost to show the consciousness, and slerp lerp back to real time."

Every seat change runs one transition, started where the change is made: `TakeSeat` (god to a seat and seat to seat,
so Fly this and every key and picker path) and `ReturnToCommand` (a seat back to the god's view). The state is
`UnitySpaceApp::seatTransition` (`UnitySpaceApp.Possession.inl/.cpp`); the possession show above plays alongside it.

1. **Time slows to a standstill.** `UpdateSeatTransition`, just before the clock advances, sets the clock's own `Speed`
   (the multiplier the eased pause drives) on a log curve: down over the first 30% of the transition to 2% of full speed,
   held there, and back up from 65%. Nothing is lost or skipped: the simulation steps as it does while a pause eases in.
   It never touches a clock the player is pausing, paused or resuming, and never runs in co-op (the clock is everyone's).
2. **The view drifts like a ghost.** The camera's own glide (`SpaceCamera`'s Travel) becomes the ghost's drift
   (`SpaceCamera::Ghost`): as long as the transition, its distance eased log-scaled about the seat (so god scale to
   cockpit scale reads evenly at true scale), its direction and its turn slerped over the whole of it, and a slow float
   across the view and a lean of a few degrees, swelling mid-way and gone at both eyes (still under Reduce motion). Going
   back to command with nothing focused, it drifts back out to the god's eye left on taking the seat
   (`SpaceCamera::GlideToPose`, kept by `RememberCommand` as an offset from its frame body), ending in the free camera there;
   with something focused it glides to it as before. The screen takes the show's wash (up to twice its peak) at the
   standstill.
3. **Back to real time.** Time picks back up to full speed as the eye lands: 1x in a seat (Fly this's, as `TakeSeat` sets
   it), the god's own warp going back to command (set as time starts to pick up, unless the player picked another meanwhile).

A seat changed again mid-way starts its drift from the standstill. **Esc or a click** skips it: the glide lands and time
is back at once (`SkipSeatTransitionOnKey`, before the commands; that Esc does not open the pause menu).
**Settings > Gameplay > Seat change transition** sets its length (2.4 s by default, up to 5; 0 is instant, the old
behaviour). A scripted run is always instant (`PlayerOptions::SeatTransitionSeconds`), so every capture, test and bench
path is unchanged. A save or rewind keyframe taken at the standstill keeps the slowed `Speed`; loading it runs at full
speed again (`ApplyLoaded`), since the transition is not carried over.

Per frame it is a handful of arithmetic: no allocation, no search. There is no post-process volume change: the
standstill's look is the HUD wash.

## Free possession (US-T952)

**Settings > Gameplay > Free possession** (`gameplay.freePossession`, kept in the profile, on by default) lets P take
any hull, a battleship or one already in a fight, at Influence 1. While it is on, `UpdateInfluence` sets
`SpaceGodInfluence::Waived` each frame, as the smoke and audit harnesses do, so `Embodiables::MayEmbody` allows the take
before its in-battle and Influence rules; the Influence meter reads FREE POSSESSION. Turned off, the waiver it gave is
taken back and the old refusals return (the dev menu's Embody limits toggle still works then). `Waived` is never saved,
so nothing reaches a save or the simulation's digest.

It is single player only. A co-op seat take is weighed by `SeatAuthority::Take` on every peer alike, so a waiver held by
one peer's profile would split the session; in a session (`CoopPeer::Current`) the setting is ignored and the limits
apply. A scripted run (`PlayerOptions::ScriptedRun`) ignores it too, so its gate is whatever the run sets.

## Checking it

```powershell
./tools/play.ps1 -Offscreen -Capture '' -Seconds 240 -ExecCmds "sg.Possess shot R:/Temp/possess quit"
```

`sg.Possess [shot <dir>] [quit]` puts a battleship, a fighter, a freighter about 380,000 km out (squawking) and a courier
of the same flag running silent 260,000 km out by a world with a defence platform, then screenshots (`possess-*.png`):
the god's view; the battleship's helm taken at 0.35, 0.7, 1.05 and 1.5 s into the show; the helm's own view; a fight
started by the platform and the platform taken (0.6 s in, then its view); a repair worker taken (0.7 s in, then its
view); the helm again and then let go (0.3 and 0.8 s out, then the god's view again). Each shot logs the view's kind, how
far it is in, the show's time, the motes lit, the rim and sheen, the field of view, and what the picture holds of the
freighter, the courier and the fighter.

## Limits

- Weapons fire, wrecks, launched craft, mechs and the satellites are not filtered by the picture: a fight's fire shows
  from an undetected hull.
- Line of sight (a world between the seat and a contact) and nebulae count only at the helm (the simulation's picture);
  a seat's view-side picture ignores them.
- A track's light-lagged place is its course run back by the delay, not its true past (its schedule is not asked, so no
  fleet cache is stepped); turning hard within the delay is not shown.
- Bodies, stations and their labels are not filtered: every seat has the charts.
- The energy is drawn with the existing additive Glow quads and the hull's rim term; there is no new material.

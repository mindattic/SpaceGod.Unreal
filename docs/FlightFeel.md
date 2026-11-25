# Flight feel: targets per family (US-T734)

What flying and fighting should feel like at the stick, as numbers, for each `FlightFamily`; where each number
stands today in code; and how `tools/feel-report.py` measures it. US-T736 (mouse flight and assist on), US-T737
(assist off and drift), US-T738 (boost and power pips) and US-T740 (device curves) build to this page: each has its
list of concrete changes at the end.

The rule above all of it is the owner's: fast and reactive. Every change below is per-hull arithmetic in the
flight model's step (already O(1) a hull) or a setting read; nothing here adds game-thread work that grows with the
scene. The crews fly as they do now: every piloted-only change is gated on `PilotInput::Piloted` (as `LightFighterFeel`
and `HeavyFighterFeel` already are), so `tools/bench.ps1` keeps matching `Bench/expected-digests.json`.

## What the reference games teach

Not their numbers (we fly at true scale, in km/s), their shape: how quickly a hull answers, and what the pilot is
trading.

- **FreeSpace 2**: assist is always on and the hull never slides; the nose has a soft, short wind-up and a soft stop,
  so a turn reads as a craft with mass, not a cursor. Turning fights are won on turn rate and energy (pips to
  engines or guns), and the afterburner is a pool you spend and wait for.
- **I-War 2**: Newtonian by default. Velocity is kept until you spend thrust on it; the computer will hold attitude
  for you, but where you are going and where you are pointing are two different things. The skill is planning the
  burn.
- **Freelancer**: the mouse cursor is the stick. Leave it near the middle and the ship holds course; push it out and
  the nose follows at a rate that grows with the offset. The ship levels itself, and speed changes come at once.
  Nothing to learn before you are flying well.
- **Star Wars: Squadrons**: one hand on the power: shift it to engines and you go faster and boost longer, to guns
  and you fire longer, to shields and you live longer. Boost is a resource that the pips refill; a boost held into a
  hard turn is a drift that swings the tail round.
- **Star Conflict**: the camera aims freely and the hull chases it; an afterburner trades turn for speed. A light
  fighter is twitchy, a strafe is a real dodge.
- **Chorus**: hold a key and the assist lets go: the craft keeps going along its line while you spin it to face
  anything, then let go and it bites into the new heading with a kick. Making a pass and shooting back behind you
  is one move.

Our families already map onto these: the light fighter, interceptor and runner are the Star Conflict free aim
(`PilotControl::FreeAim`, the string aim and whip); the heavy fighter and bomber fly nose-first (`DirectNose`);
assist off is the Newtonian `PilotFlight::Step`; drift (R held, `Drift`) is the Chorus move. What is missing is the
Freelancer-style cursor stick, quick speed changes, a boost pool fed by the pips, and per-device curves.

## Current values, read from code

Representative class per family, from `Combat/ShipCatalog.cpp` (the handling table), `Combat/CombatSpecs.cpp` (mass and
thrust, `HullDynamics::Of`: MN over kt is m/s²) and `Combat/FlightModels.h`. All flight knobs (`tune.flight.<family>.*`,
`PilotTuning::Of`) at 1x. Derived columns are arithmetic on those numbers, not measured:

- **0 to top** = top speed over main-drive acceleration (the bomber's also over its 0.6 `ThrottleAuthority`).
- **180 deg** = 180 over the turn rate, plus the spin-up floor; for the free-aim families, with the whip
  (`LightFighterFeel::Whip`, full 1.8x past 120 deg off the nose, easing in from 30 deg).

| Family (class) | Control | Top km/s | Turn / roll deg/s | Main / lateral accel m/s² | 0 to top s | 180 deg s | Spin-up s | Strafe | Boost x / charge s | Cruise x / spool s |
|---|---|---|---|---|---|---|---|---|---|---|
| LightFighter (Fighter) | FreeAim | 0.42 | 165 / 247.5 | 55 / 27.5 | 7.6 | ~0.85 (1.2 no whip) | 0.08 | 0.5 | 2.3 / 15 | 9 / 1.8 |
| Interceptor (Interceptor) | FreeAim | 0.48 | 180 / 270 | 70 / 38 | 6.9 | ~0.8 | 0.08 | 0.6 | 2.4 / 12 | 9 / 1.6 |
| Interceptor (Recon) | FreeAim | 0.46 | 160 / 240 | 68 / 34 | 6.8 | ~0.9 | 0.08 | 0.6 | 2.4 / 16 | 9 / 1.6 |
| Runner (Courier) | FreeAim | 0.52 | 120 / 180 | 50 / 25 | 10.4 | ~1.4 | 0.2 | 0.35 | 2.4 / 45 | 10 / 1.2 |
| HeavyFighter (Gunship) | DirectNose | 0.40 | 115 / 172.5 | 50 / 25 | 8.0 (settle 0.9) | 1.8 | 0.22 | 0.45 | 2.0, surge 1.3 / 20 | 9 / 2.2 |
| Bomber (Bomber) | DirectNose | 0.30 | 85 / 127.5 | 33 / 12 | 15 | 2.1 + hull's own | hull's own | 0.25 | 1.6 / 12 | 8 / 2.4 |
| Hauler (Freighter, empty) | Hauler | 0.18 | 75 / 112.5 | 15 / 8 | 12 | 2.4 + hull's own | hull's own | hauler's | 1.6 / 36 x hauler fraction | 8 / 6.0 |
| Capital (Cruiser) | Helm | 0.14 | fitted to come-about | 5 / 2 | 28 (telegraph spool 4) | come-about 10 (default) | fitted | 0.15 slip | 1.4 / 28 (Flank) | 6 / 4.0 |

The light fighter's turn above its corner speed (0.6 of top) tapers toward 0.6 of the rate at full boost speed: about
150 deg/s at top speed. Interceptor: corner 0.75, 0.75 at boost. Runner: corner 0.4, 0.45 at boost.

Other numbers the feel depends on:

| What | Now | Where |
|---|---|---|
| Light fighter dash (lateral boost) | 0.6 of boosted forward, 0.8 of main push | `LightFighterFeel::Dogfighter` |
| Heavy fighter boost kick / bleed | 0.12 s time constant / 3.5 s | `HeavyFighterFeel::Default` |
| Boost recharge | half the drain rate (a 15 s charge refills in 30 s) | `PilotFlight::RechargeFraction` |
| Engine pips | top speed, every thruster's push and boost recharge, x0.5 .. x1.5 (a quarter a pip over two) | `CraftPower`, `PowerDistribution` |
| Drift | spin-up 0.12 s, swing 1.3x the turn rate, boost-out 0.6 s | `Drift` |
| Heavy string aim | 25 deg lead at 1.3x the hull's turn rate | `PilotAim::HeavyMaxLeadDeg`, `HeavyLookRateFactor` |
| Hauler mouse | 0.6 of the fighter's | `HelmControls::HaulerLookFactor` |
| Mouse | raw counts, x `camera.lookSensitivity` 0.12 deg a count, smoothing 0 (off) by default | `SpaceCamera` |
| Pilot chase camera | 20 Hz on fighters, 12 on heavy fighters, bombers and haulers, 6 on capitals (US-T819) | `camera.pilotResponse`, `camera.pilotResponseHeavy`, `camera.pilotResponseHauler`, `camera.pilotResponseCapital` |
| Pad | radial dead zone 0.2, Linear / Smooth / Precise curve, 180 deg/s at full deflection | `Input/Gamepad` |
| Joystick / HOTAS / throttle axis | none | - |

What the numbers say: turn rates and spin-ups are already arcade-quick (the free-aim 180 is under a second). What
is slow is speed: every small craft takes 7 to 15 s to reach top speed on its own drive, so a throttle change reads
as a long ease, not an answer. And boost charges are long (15 to 45 s) with a slow refill, so boost is rarely the
decision it is in Squadrons.

## Targets

Speeds stay true scale: the targets are times, which is what the hand feels. Every target is a setting (a
`tune.flight.*` knob or a handling field), not a constant, so the owner tunes the final values in play.

### Assist on

| Family | 0 to top s | Top to stop s | 180 deg s (corner speed / top) | Roll 360 s | Spin-up s | Overshoot on stop |
|---|---|---|---|---|---|---|
| LightFighter | 2.5 | 2.0 | 0.85 / 1.2 | 1.2 | 0.08 | under 2 deg |
| Interceptor | 2.2 | 1.8 | 0.8 / 1.0 | 1.1 | 0.08 | under 2 deg |
| Runner | 3.0 | 3.0 | 1.4 / 2.0 | 1.6 | 0.15 | under 2 deg |
| HeavyFighter | 3.5 | 3.0 | 1.6 / 1.6 | 1.8 | 0.15 | under 1 deg |
| Bomber | 5.0 | 4.5 | 2.1 / 2.1 | 2.5 | 0.25 | under 1 deg |
| Hauler (empty / full) | 8 / 14 | 7 / 12 | 2.4 / 2.4 (x1.8 full) | 3.0 | 0.4 | under 1 deg |
| Capital | telegraph step in 4 (spool, kept) | 6 to Stop | come-about per class (10 cruiser, kept) | slow, per class | fitted | none |

- The light families keep today's turn: the 180 is already where it should be. Roll rises (247.5 to 300 deg/s
  on the fighter) so a roll-and-pull is one quick move.
- The throttle is where the change is: the small craft answer a throttle change in a third of the time they do now.
  This is the flight computer's push (the light fighter's `Grip` already lets assist push across the nose harder than
  the manoeuvre thrusters): a piloted-only floor on the assist's push along the nose, as `SpinUpS` is a floor on the
  attitude thrusters', so the crews and the Bench keep their numbers.
- Speed-matching: with a lock held, one key sets the throttle's set point to the target's speed (Freelancer's match
  speed), so a chase is aiming, not throttle-juggling.

### Assist off (I-War 2) and drift (Chorus)

| What | Target | Now |
|---|---|---|
| Velocity with assist off and no thrust | kept to 0.1% over 10 s (gravity aside) | kept (Newtonian `PilotFlight::Step`) |
| Rotation with assist off | still rate-held, the same turn rate and spin-up as assist on | rate-held under a player's hands on `AssistThrottle::SpinUp` (US-T828); crews keep the hull's own torque |
| Toggling assist back on | no rotation hitch; back on the set point within the assist-on 0-to-top time | thrust-bound (7 s or more) |
| Drift flip (180 deg, light fighter) | 0.75 s | 165 x 1.3 = 214 deg/s plus 0.12 s: about 0.96 s |
| Drift velocity kept | to 0.1% (gravity aside) | yes |
| Boost-out on release | 90% of top speed on the new facing in 0.8 s | 0.6 s afterburner, then the thrust-bound ease |
| Velocity vector on the HUD | shown whenever velocity is more than 5 deg off the nose | not shown |

### Boost and power (Squadrons)

| What | Target | Now |
|---|---|---|
| Boost charge, small craft | 5 s at the even share (runner 10 s) | 12 to 45 s |
| Boost refill from empty | 6 s at two engine pips, 4 s at four, 12 s at one | 2x the charge (24 to 90 s) |
| Boost kick | 90% of boost speed in 1.0 s (light), 1.4 s (heavy) | thrust-bound; heavy 0.12 s constant |
| Boost let go | the extra speed bleeds off over 2 s (light), 3.5 s (heavy, kept) | light: braked at assist's push |
| Engine pips | top speed x0.8 .. x1.2, accel and boost refill x0.5 .. x1.5 | all three x0.5 .. x1.5 |
| Pip change | takes hold on the next frame, with an audible and visual cue | no dedicated cue |

Top speed narrows to x0.8 .. x1.2 so four pips to weapons does not leave a fighter crawling at half speed (a 0.21
km/s fighter is a target, not a choice); acceleration and boost refill keep the full swing, so engines still decide
who wins a chase.

### Guns (US-T747)

| What | Target | Now |
|---|---|---|
| Lead pip | on the locked hull (else the hostile nearest the crosshair), for the selected gun group's rounds: the last `Alt+1-9` mount fired, the main trigger's guns again once it is pulled; tagged `G<n>` while a group is picked | the fastest nose gun's catalog speed, `tune.combat.projectileSpeed` ignored |
| Convergence | the crosshair's line where it meets a hostile, else the locked hull's range when inside the gun's reach, else the gun's reach | the hit or the gun's reach |
| Fighter guns' flight time | about 0.5 s to their 1.6 km reach (3.5 km/s), under 0.3 s at a 1 km dogfight | kept: catalog `MuzzleSpeedKmS`, shared with the crews, scaled by `tune.combat.projectileSpeed` |
| Overheat, guns held down | 5 s for the light fighter's guns at the even power share | never (46.7 dps x 0.004 = 0.19 a second, under the 0.2 shed) |
| Fire again after overheating | 2.5 s; cold from full in 3.8 s | 3.25 s; 5 s |

Heat is `tune.guns.heatPerDamage` 0.01, `tune.guns.heatCoolPerS` 0.26 and `tune.guns.heatRecoverBelow` 0.35
(`Gunnery`); only the player's guns build heat (`Battle::FirePlayerGun`), so the crews and the Bench keep their
numbers. Hulls with more guns on the trigger (the Gunship's railgun and turrets) heat faster in proportion to their
damage, as before.

### Input and latency

| What | Target | Now |
|---|---|---|
| Input to the hull's command | the same frame (no queued frame) | measured by feel-report (US-T819) |
| Pilot chase camera | 20 Hz on fighters (50 ms), 12 on heavy and haulers, 6 on capitals | as the target (US-T819) |
| Mouse smoothing | off by default (kept) | off |
| Hand to screen at 60 fps | 50 ms or under (3 frames) | measured by feel-report (US-T819) |
| Mouse flight (virtual stick) | dead zone 3% of the half-screen, full rate at 40%, curve exponent 1.5 | none (free aim and nose only) |
| Pad / stick curves | per-axis dead zone, exponent 1 .. 3, outer dead zone 0 .. 10% | radial dead zone and three fixed curves, pad only |

## How feel-report measures it

`sg.Feel [game|real] [csv <file>] [quit]` (`Cpp/Source/SpaceGod/FeelCommand.cpp`) flies a scripted run (the god
focuses a fighter, takes its helm, thrusts, turns, rolls, boosts, lets go, takes a battleship) and writes a row a
frame: step, dt, camera mode and lock, camera position, forward and up, field of view, what is held, the show, the
pilot view, both hulls' positions and radii, and focus. `python tools/feel-report.py <csv> [--all]` reads it.

What it measures today:

| Measure | How | Answers |
|---|---|---|
| Look and roll per frame, per step | angle between consecutive camera forwards; the up's turn about the new forward | camera turn rate (deg a frame over dt) while the pilot view follows the hull |
| Camera move against the followed hull, in its radii | position delta less the hull's | chase camera lag and pops |
| Field of view change | fov delta per frame | boost and speed widening, jumps over 1 deg a frame |
| Jolts and hitches | a frame several times its neighbourhood median; a long frame whose rate is normal is a hitch | smoothness, not handling |

US-T819 added the flown hull's columns (family, nose and roof, velocity, throttle, strafe, roll, the virtual stick,
boost, assist, drift, turn and roll rate) and the scripted hand (keys held, mouse counts), with handling steps in the
run (to top, to stop, a yaw and a roll 360, each with a settle) and the report's handling section. What it does not see
yet: boost charge and power. Each follow-on task adds the columns and the report section its targets need (below).
The report then prints, per step, the measured value beside its target from this page, and flags any miss by more
than 10%:

| Target | Measured as |
|---|---|
| 0 to top, top to stop | the first frame after a full-throttle (or zero-throttle) input until speed is within 5% of the set point |
| 180 deg, roll 360 | from the input frame until the hull's forward (or up) has turned through it and its rate is under 5% of the top |
| Spin-up, overshoot | time to 90% of the top rate; how far past the commanded heading the nose goes after the input stops |
| Velocity kept | speed and direction change across an assist-off or drift step with no thrust input |
| Boost kick, bleed, refill | speed against time from boost on and off; boost charge against time |
| Hand to command | frames between an input row's change and the hull's rate starting to change (0 is the target) |
| Hand to screen | the same, to the camera's forward (frames times dt) |

## The changes, task by task

### US-T736: mouse flight and assist on

1. A virtual-stick mouse mode (Freelancer, FreeSpace 2): a setting `controls.mouseFlight` (Free aim, the current
   string aim, default for free-aim hulls; Virtual stick; Nose, the current direct-nose). In Virtual stick the cursor's
   offset from the screen's centre is the turn rate demand: a dead zone (`controls.mouseFlight.deadZone`, 3% of the
   half-screen), full rate at `controls.mouseFlight.fullAt` (40%), an exponent (1.5); fed to `PilotInput` as a rate
   rather than a `DesiredForward`. A centre ring and the cursor drawn on the HUD. The same in a cockpit and at a
   fighter's or hauler's helm (US-T817, `ShipHelm::Fly`); a capital's helm keeps the mouse as its look-around.
2. A piloted-only floor on assist's push along the nose and back (a `ThrottleS` beside `SpinUpS` on
   `LightFighterFeel` and the `HeavyFighterFeel`, a bomber and hauler equivalent), sized to the 0-to-top and
   top-to-stop targets above; the crews keep the hull's own thrust.
3. Light fighter roll to 300 deg/s (its catalog row's roll, which no crew reads differently: check the Bench, or
   gate as a piloted-only factor if the digests move).
4. Match speed: a binding that sets the set point to the locked target's speed.
5. Auto-level roll option (`controls.autoLevel`, off by default): with assist on and no roll input, the roof eases
   toward the system's north (or the locked capital's up) over 1 s.
6. Pilot chase response by family (`camera.pilotResponse` becomes the fighter's; heavy, hauler and capital get their
   own knobs at the targets above).
7. sg.Feel: columns for the hull's forward and up, velocity, throttle, assist, and the raw input; feel-report's
   handling section for 0 to top, top to stop, 180, roll, spin-up, overshoot, hand to command and to screen.

### US-T737: assist off and drift

1. Assist off keeps rotation rate-held: the family's turn rate and spin-up floor apply (today it falls to the hull's
   own torque); translation stays fully Newtonian.
2. A velocity-vector marker on the HUD (prograde and retrograde) when velocity is over 5 deg off the nose, in both
   assist off and drift. In a cockpit (US-T830, `Hud::DrawSeatVelocity`) past `interface.velocityMarkerAngle` (5 deg);
   the helm's flight markers show them always.
3. Toggling assist back on: re-acquire the set point at the assist-on push (US-T736's floor), no rotation step.
4. Drift: `Drift::TurnFactor` 1.3 to 1.6 and `Drift::SpinUpS` 0.12 to 0.1 as settings, for the 0.75 s flip; the
   boost-out (`BoostOutS`) reaching 90% of top speed on the new facing in 0.8 s.
5. feel-report: velocity-kept and drift-flip measures, from the columns US-T736 adds.

### US-T738: boost and power pips

1. Boost as an energy pool per craft: a charge of 5 s (runner 10 s) at the even share, refilling in 6 s at two
   engine pips (4 s at four, 12 s at one); the catalog's `BoostDurationS` becomes the pool's size, and
   `PilotFlight::RechargeFraction` the pips' business.
2. Engine pips: top speed x0.8 .. x1.2, push and refill keep x0.5 .. x1.5 (`CraftPower`, `PowerDistribution`).
3. A boost kick under the player's hands for the light families like the heavy fighter's (`BoostKickS`), and a
   light-family bleed of 2 s once let go.
4. A cue on a pip change (a HUD flash of the bars and a click), and the pool on the HUD.
5. Weapons and shields pips stay as they are (`CraftPower`'s rate of fire and shield regrowth).
6. feel-report: boost kick, bleed and refill, from columns for boost, the pool and the split.

### US-T740: device curves

1. Per-device, per-axis curves in Settings > Controls: inner dead zone, outer dead zone (0 .. 10%), exponent
   (1 .. 3), invert; the mouse's virtual stick (US-T736) included, the pad's `ResponseCurve` kept as presets of it.
2. Joystick and HOTAS axes through Enhanced Input: pitch, yaw, roll, strafe and a throttle axis that sets the
   throttle absolutely (today a held key opens it at `FighterSeat::ThrottlePerSecond`, full range in 2 s).
3. A curve preview in the Controls options: the axis's raw input against the output, live.
4. The scripted runs keep linear curves and no dead zone, as the pad's settings do today.

### US-T754: AI dogfight behaviours

The crews' dogfight (`DogfightManeuvers`) is flown by a person, not a turret, and this one does change the Bench
digests: rebaseline them deliberately.

1. Two more manoeuvres: **Break** (a hard full-rate turn across an attacker's line, toward its side) and **Extend**
   (fast and the attacker not gaining: out of its reach under boost, then round again). On the tail, a sharp pilot
   breaks or extends, a green one jinks; flip-and-burn and scissors as before.
2. Reaction: a new attacker on the tail is acted on after the pilot's own lag (half its `Reaction`, 0.2-0.9 s; a
   fleet light fighter 0.45 s).
3. Aim error: the nose wanders smoothly round the pip, 3.5 deg for no skill down to a tenth of it for an ace, at
   0.6 Hz, so the AI misses believably and the nose never snaps.
4. Difficulty: against a hull a player holds, aim error divides by the hostile-accuracy scale and reaction multiplies
   by a new hostile-reaction scale (Story 1.6x, Hard 0.65x, `gameplay.custom.reaction`); exactly 1 at Normal.

## Not changed

The crews' flight (the Bench digests), the capital's telegraph and come-about, the hauler's mass weighting and
docking mode, the string aim's lead and whip, and the cruise drive's tiers.

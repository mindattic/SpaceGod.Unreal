"""US-T463: read sg.Feel's CSV (one row a frame) and name the jolts.

For each frame: how far the view turned (deg; split into the look's swing and the roll about it), how far the camera
moved against what it is following (the hull held, else the focused hull, else the scene), in that hull's radii, and
the field of view's change. A jolt is a frame whose turn or move is several times both its neighbourhood's median and
above a floor: a pop, a snap, a hitch. Each step also gets its peak rates, so a transition that is merely abrupt (a
rate that starts or stops in one frame) shows too.

    python tools/feel-report.py Saved/Logs/feel-game.csv [--all]
"""
import csv
import math
import statistics
import sys


def vec(r, *names):
    return [float(r[n]) for n in names]


def sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def norm(a):
    return math.sqrt(dot(a, a))


def angle(a, b):
    na, nb = norm(a), norm(b)
    if na < 1e-12 or nb < 1e-12:
        return 0.0
    return math.degrees(math.acos(max(-1.0, min(1.0, dot(a, b) / (na * nb)))))


def roll_change(f0, u0, f1, u1):
    # The up's turn about the look, once the look's own swing is taken out: project both ups onto the plane of the new look.
    def proj(u, f):
        k = dot(u, f) / max(dot(f, f), 1e-12)
        return [u[i] - f[i] * k for i in range(3)]

    return angle(proj(u0, f1), proj(u1, f1))


def main():
    path = sys.argv[1]
    show_all = '--all' in sys.argv
    rows = list(csv.DictReader(open(path, newline='')))
    if len(rows) < 3:
        print('too few frames')
        return 1
    frames = []
    for i, r in enumerate(rows):
        c = vec(r, 'cx', 'cy', 'cz')
        f = vec(r, 'fx', 'fy', 'fz')
        u = vec(r, 'ux', 'uy', 'uz')
        a, ar = vec(r, 'ax', 'ay', 'az'), float(r['ar'])
        b, br = vec(r, 'bx', 'by', 'bz'), float(r['br'])
        held = r['held']
        # US-T477: CSVs before it have no focus column; a god then counts as focused on the fighter while in Focus mode.
        focus = r.get('focus') or ('fighter' if r['mode'] == 'Focus' else 'none')
        frames.append(dict(step=r['step'], dt=float(r['dt']), mode=r['mode'], held=held, focus=focus, c=c, f=f, u=u,
                           fov=float(r['fov']), hull={'fighter': (a, ar), 'battleship': (b, br)},
                           show=r['show'] == '1', pv=r['pilotView'] == '1'))

    def followed(p, q):
        # US-T477: the hull the view follows across the pair: the one held (on a hand-off, the one taken, or the one let
        # go), else the one the god's camera is focused on, else none (the scene).
        for f in (q, p):
            if f['held'] in f['hull']:
                return f['held']
        for f in (q, p):
            if f['focus'] in f['hull']:
                return f['focus']
        return None

    out = []
    for i in range(1, len(frames)):
        p, q = frames[i - 1], frames[i]
        look = angle(p['f'], q['f'])
        roll = roll_change(p['f'], p['u'], q['f'], q['u'])
        subj = followed(p, q)
        rad = q['hull'][subj or 'fighter'][1]
        rad = max(rad, 1e-12)
        if subj is not None:
            # Against the hull followed, in both frames: a floating-origin shift moves camera and hull alike and cancels.
            move = norm(sub(sub(q['c'], q['hull'][subj][0]), sub(p['c'], p['hull'][subj][0]))) / rad
        else:
            # The free camera, against the scene. An origin shift moves both hulls by the same step: take it out.
            dc = sub(q['c'], p['c'])
            da = sub(q['hull']['fighter'][0], p['hull']['fighter'][0])
            db = sub(q['hull']['battleship'][0], p['hull']['battleship'][0])
            shift = da if norm(da) > 0 and norm(sub(da, db)) < 0.05 * norm(da) else [0.0, 0.0, 0.0]
            move = norm(sub(dc, shift)) / rad
        # US-T479: the row's dt is the wall time since the last sample, which the engine hands the *next* frame's update:
        # the motion between rows p and q was made with p's dt (a 210 ms row is followed by the 10x move, not paired with it).
        out.append(dict(i=i, step=q['step'], dt=p['dt'] if i > 1 else q['dt'], look=look, roll=roll, move=move, dfov=q['fov'] - p['fov'],
                        mode=q['mode'], held=q['held'] if subj is not None or q['held'] != 'god' else 'god(scene)', show=q['show'], pv=q['pv'], fmode=p['mode'] + '>' + q['mode'] if p['mode'] != q['mode'] else q['mode']))

    def spikes(key, floor, k=4.0, w=6):
        found = []
        for j, o in enumerate(out):
            lo, hi = max(0, j - w), min(len(out), j + w + 1)
            near = [out[m][key] for m in range(lo, hi) if m != j]
            if not near:
                continue
            med = statistics.median(near)
            if o[key] > floor and o[key] > k * max(med, 1e-9):
                found.append((o, med))
        return found

    print(f'{len(frames)} frames, {sum(o["dt"] for o in out):.1f} s, median dt {statistics.median(o["dt"] for o in out) * 1000:.1f} ms')
    print()
    print('per step: peak look deg/frame, peak roll deg/frame, peak move radii/frame, fov range')
    steps = []
    for o in out:
        if not steps or steps[-1][0] != o['step']:
            steps.append((o['step'], []))
        steps[-1][1].append(o)
    for name, os_ in steps:
        fovs = [o['dfov'] for o in os_]
        print(f'  {name:24s} n={len(os_):4d}  look {max(o["look"] for o in os_):7.2f}  roll {max(o["roll"] for o in os_):6.2f}'
              f'  move {max(o["move"] for o in os_):8.3f}  dfov {min(fovs):+.2f}..{max(fovs):+.2f}')
    print()
    for key, floor, label in (('look', 1.5, 'look snaps (deg)'), ('roll', 1.0, 'roll snaps (deg)'), ('move', 0.08, 'position pops (radii)')):
        found = spikes(key, floor)
        # US-T479: a hitch is a long frame whose motion is its rate held through it (per second it is no spike): the
        # frame stalled, the view did not jolt. Named apart from the real jolts.
        def hitch(o):
            j = o['i'] - 1
            near = [out[m]['dt'] for m in range(max(0, j - 6), min(len(out), j + 7)) if m != j]
            mdt = statistics.median(near) if near else o['dt']
            if o['dt'] < 2.0 * mdt:
                return False
            rates = [out[m][key] / max(out[m]['dt'], 1e-6) for m in range(max(0, j - 6), min(len(out), j + 7)) if m != j]
            return o[key] / max(o['dt'], 1e-6) < 2.0 * max(statistics.median(rates), 1e-9)
        real = [(o, med) for o, med in found if not hitch(o)]
        print(f'{label}: {len(real)} (+{len(found) - len(real)} hitch)')
        for o, med in found[: (999 if show_all else 25)]:
            tag = f'  HITCH ({o["dt"] * 1000:.0f} ms)' if hitch(o) else ''
            print(f'  frame {o["i"]:5d} [{o["step"]}] {o[key]:.3f} vs median {med:.3f}  mode {o["fmode"]} held {o["held"]}'
                  f'{" show" if o["show"] else ""}{" pilotView" if o["pv"] else ""}{tag}')
    fovj = [o for o in out if abs(o['dfov']) > 1.0]
    print(f'fov jumps > 1 deg/frame: {len(fovj)}')
    for o in fovj[:25]:
        print(f'  frame {o["i"]:5d} [{o["step"]}] {o["dfov"]:+.2f}')
    if 'family' in rows[0]:
        handling(rows)
    if 'pool' in rows[0]:
        boost(rows)
    return 0


# US-T819: docs/FlightFeel.md's assist-on targets, s (overshoot in deg), per FlightFamily.
TARGETS = {
    'LightFighter': dict(top=2.5, stop=2.0, turn180=0.85, roll360=1.2, spinup=0.08, overshoot=2),
    'Interceptor': dict(top=2.2, stop=1.8, turn180=0.8, roll360=1.1, spinup=0.08, overshoot=2),
    'Runner': dict(top=3.0, stop=3.0, turn180=1.4, roll360=1.6, spinup=0.15, overshoot=2),
    'HeavyFighter': dict(top=3.5, stop=3.0, turn180=1.6, roll360=1.8, spinup=0.15, overshoot=1),
    'Bomber': dict(top=5.0, stop=4.5, turn180=2.1, roll360=2.5, spinup=0.25, overshoot=1),
    'Hauler': dict(top=8.0, stop=7.0, turn180=2.4, roll360=3.0, spinup=0.4, overshoot=1),
    'Capital': dict(stop=6.0),
}

# US-T821: docs/FlightFeel.md's boost targets, s: the kick to 90% of boost speed, the bleed once let go, the pool's charge
# at the even share. The refill's target follows the split: 6 s at two engine pips, over the engines' factor.
BOOST_TARGETS = {
    'LightFighter': dict(kick=1.0, bleed=2.0, pool=5.0),
    'Interceptor': dict(kick=1.0, bleed=2.0, pool=5.0),
    'Runner': dict(kick=1.0, bleed=2.0, pool=10.0),
    'HeavyFighter': dict(kick=1.4, bleed=3.5, pool=5.0),
    'Bomber': dict(pool=5.0),
}
REFILL_EVEN_S = 6.0


def handling(rows):
    """The flown hull's handling, from the columns US-T819 added: per step, 0 to top and top to stop on a throttle step, the
    spin-up, overshoot and the 180 (or roll 360) a turn's peak rate gives; then hand to command and hand to screen."""
    fr = []
    t = 0.0
    for i, r in enumerate(rows):
        # The motion in row i was made with row i-1's dt (see US-T479 above).
        t += float(rows[i - 1]['dt']) if i > 0 else 0.0
        fr.append(dict(step=r['step'], t=t, family=r['family'], hf=vec(r, 'hfx', 'hfy', 'hfz'), hu=vec(r, 'hux', 'huy', 'huz'),
                       speed=norm(vec(r, 'vx', 'vy', 'vz')), throttle=float(r['throttle']), roll=float(r['roll']),
                       stick=(float(r['strafeR']), float(r['strafeU']), float(r['stickR']), float(r['stickU']), float(r['boost'])),
                       assist=r['assist'] == '1', turn=float(r['turnRate']), rollRate=float(r['rollRate']),
                       hand=(r['keys'], float(r['mx']) != 0 or float(r['my']) != 0), mouse=(float(r['mx']), float(r['my'])),
                       f=vec(r, 'fx', 'fy', 'fz'), u=vec(r, 'ux', 'uy', 'uz')))

    steps = []
    for k, f in enumerate(fr):
        if f['family'] == 'none':
            continue
        if not steps or steps[-1][0] != f['step']:
            steps.append((f['step'], []))
        steps[-1][1].append(k)

    print()
    print('handling (US-T819), against docs/FlightFeel.md; ! marks a miss by more than 10%')

    def report(step, family, what, got, unit='s'):
        want = TARGETS.get(family, {}).get(what)
        if got is None:
            print(f'  {step:24s} {family:13s} {what:9s} not reached' + (f'   target {want:g} {unit}' if want is not None else ''))
            return
        flag = ' !' if want is not None and got > want * 1.1 else ''
        print(f'  {step:24s} {family:13s} {what:9s} {got:7.3f} {unit}' + (f'   target {want:g} {unit}' if want is not None else '') + flag)

    for n, (name, ks) in enumerate(steps):
        family = fr[ks[0]]['family']
        after = steps[n + 1][1] if n + 1 < len(steps) and steps[n + 1][0].endswith('settle') else []
        if 'to top' in name:
            start = next((k for k in ks if fr[k]['throttle'] >= 0.99), None)
            if start is not None:
                tail = [fr[k]['speed'] for k in ks if fr[k]['t'] >= fr[ks[-1]]['t'] - 0.5]
                settled = tail and max(tail) - min(tail) <= 0.01 * max(max(tail), 1e-12)
                top = max(tail) if tail else 0
                hit = next((k for k in ks if k >= start and fr[k]['speed'] >= 0.95 * top), None) if settled else None
                report(name, family, 'top', fr[hit]['t'] - fr[start]['t'] if hit is not None else None)
        if 'to stop' in name:
            k0 = next((k for k in ks if fr[k]['throttle'] == 0), ks[0])
            v0 = fr[k0]['speed']
            if fr[k0]['throttle'] == 0 and v0 > 0:
                hit = next((k for k in ks if fr[k]['speed'] <= 0.05 * v0), None)
                report(name, family, 'stop', fr[hit]['t'] - fr[k0]['t'] if hit is not None else None)
        for rate, full, what in (('turn', 180.0, 'turn180'), ('rollRate', 360.0, 'roll360')):
            if what == 'turn180' and 'yaw' not in name and 'turn' not in name and 'pitch' not in name:
                continue
            if what == 'roll360' and 'roll' not in name:
                continue
            # The hand on it: Ctrl+A/D rolls, the mouse turns.
            live = [k for k in ks if ('Ctrl' in fr[k]['hand'][0] if what == 'roll360' else fr[k]['hand'][1])]
            if not live:
                continue
            peak = max(abs(fr[k][rate]) for k in ks)
            if peak <= 0:
                continue
            start = live[0]
            up = next((k for k in ks if k >= start and abs(fr[k][rate]) >= 0.9 * peak), None)
            spin = fr[up]['t'] - fr[start]['t'] if up is not None else None
            report(name, family, 'spinup', spin)
            report(name, family, what, full / peak + (spin or 0.0))
            if after and what == 'turn180':
                # How far the nose swings on past where it comes to rest once the hand lets go.
                window = [k for k in ks if k > live[-1]] + after
                if window:
                    k0, rest = live[-1], fr[window[-1]]['hf']
                    total = angle(fr[k0]['hf'], rest)
                    over = max(angle(fr[k0]['hf'], fr[k]['hf']) for k in window) - total
                    report(name, family, 'overshoot', max(0.0, over), 'deg')

    # Hand to command and to screen: from a row whose scripted hand changed (the hand is recorded on the row whose frame
    # flew it), the frames until the helm's command or the hull's rate changes, and until the camera turns.
    def command(f):
        return (round(f['throttle'], 4), round(f['roll'], 4), tuple(round(x, 4) for x in f['stick']))

    cmd, scr = [], []
    for k in range(1, len(fr)):
        p, q = fr[k - 1], fr[k]
        if q['family'] == 'none' or p['family'] == 'none' or (q['hand'] == p['hand'] and q['mouse'] == p['mouse']):
            continue
        c = next((m - k for m in range(k, min(len(fr), k + 30))
                  if command(fr[m]) != command(fr[m - 1]) or abs(fr[m]['turn'] - fr[m - 1]['turn']) > 0.5
                  or abs(fr[m]['rollRate'] - fr[m - 1]['rollRate']) > 0.5), None)
        s = next((m - k for m in range(k, min(len(fr), k + 30))
                  if angle(fr[m]['f'], fr[m - 1]['f']) > 0.01 or roll_change(fr[m - 1]['f'], fr[m - 1]['u'], fr[m]['f'], fr[m]['u']) > 0.01), None)
        dt = float(rows[k]['dt'])
        if c is not None:
            cmd.append((c, dt))
        if s is not None:
            scr.append((s, dt))
    for label, xs, want in (('hand to command', cmd, 'target 0 frames'), ('hand to screen', scr, 'target 50 ms')):
        if not xs:
            print(f'  {label}: no hand changes seen')
            continue
        frames = [x for x, _ in xs]
        ms = [(x + 1) * d * 1000 for x, d in xs]
        print(f'  {label}: {len(xs)} changes, median {statistics.median(frames):.0f} frames, worst {max(frames)} frames'
              f' ({max(ms):.0f} ms)   {want}')


def boost(rows):
    """The boost, from the columns US-T821 added: the kick (boost on, from its speed to 90% of the boosted top), the bleed
    (boost off, until 90% of the extra speed is gone), and the pool's charge and refill as the rate the pool moved at while
    draining and refilling, scaled to a whole pool (so a step cut short still measures)."""
    fr = []
    t = 0.0
    for i, r in enumerate(rows):
        t += float(rows[i - 1]['dt']) if i > 0 else 0.0
        fr.append(dict(step=r['step'], t=t, family=r['family'], speed=norm(vec(r, 'vx', 'vy', 'vz')), boost=r['boost'] == '1',
                       pool=float(r['pool']), split=int(r['split']), engines=float(r['engines']),
                       poolS=float(r['poolS']), refillS=float(r['refillS'])))
    steps = {}
    for k, f in enumerate(fr):
        if f['family'] != 'none':
            steps.setdefault(f['step'], []).append(k)

    print()
    print('boost and power (US-T821), against docs/FlightFeel.md; ! marks a miss by more than 10%')

    def report(step, family, what, got, want):
        tail = f'   target {want:g} s' if want is not None else ''
        if got is None:
            print(f'  {step:24s} {family:13s} {what:7s} not reached{tail}')
            return
        flag = ' !' if want is not None and got > want * 1.1 else ''
        print(f'  {step:24s} {family:13s} {what:7s} {got:7.3f} s{tail}{flag}')

    def split(f):
        code = f['split']
        return f'{code // 25}/{code // 5 % 5}/{code % 5}'

    kick = steps.get('fighter boost kick', [])
    if kick:
        family = fr[kick[0]]['family']
        on = next((k for k in kick if fr[k]['boost']), None)
        if on is not None:
            v0, peak = fr[on]['speed'], max(fr[k]['speed'] for k in kick)
            hit = next((k for k in kick if k >= on and fr[k]['speed'] >= v0 + 0.9 * (peak - v0)), None) if peak > v0 else None
            report('fighter boost kick', family, 'kick', fr[hit]['t'] - fr[on]['t'] if hit is not None else None,
                   BOOST_TARGETS.get(family, {}).get('kick'))
            bleed = steps.get('fighter boost bleed', [])
            if bleed and peak > v0:
                off = bleed[0]
                extra0 = fr[off]['speed'] - v0
                hit = next((k for k in bleed if fr[k]['speed'] - v0 <= 0.1 * extra0), None) if extra0 > 0 else None
                report('fighter boost bleed', family, 'bleed', fr[hit]['t'] - fr[off]['t'] if hit is not None else None,
                       BOOST_TARGETS.get(family, {}).get('bleed'))

    for name, what, sign in (('fighter boost drain', 'pool', -1), ('fighter boost refill', 'refill', 1)):
        ks = steps.get(name, [])
        if not ks:
            continue
        f0 = fr[ks[0]]
        family = f0['family']
        # From the first frame the pool moves the right way to the last, as seconds a whole pool.
        live = [k for k in ks[1:] if (fr[k]['pool'] - fr[k - 1]['pool']) * sign > 0]
        got = None
        if live:
            a, b = live[0] - 1, live[-1]
            moved = (fr[b]['pool'] - fr[a]['pool']) * sign
            if moved > 0:
                got = (fr[b]['t'] - fr[a]['t']) / moved
        want = BOOST_TARGETS.get(family, {}).get('pool')
        if what == 'refill' and want is not None:
            want = REFILL_EVEN_S / max(f0['engines'], 0.1)
        report(name, family, what, got, want)
        print(f'  {"":24s} {"":13s} split {split(f0)} (weapons/shields/engines), engines x{f0["engines"]:.2f},'
              f' seat reads pool {f0["poolS"]:g} s, refill {f0["refillS"]:g} s')


if __name__ == '__main__':
    sys.exit(main())

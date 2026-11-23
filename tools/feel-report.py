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
        subj, rad = (a, ar) if held == 'fighter' else (b, br) if held == 'battleship' else (a, ar)
        frames.append(dict(step=r['step'], dt=float(r['dt']), mode=r['mode'], held=held, c=c, f=f, u=u, fov=float(r['fov']),
                           rel=sub(c, subj), rad=max(rad, 1e-12), show=r['show'] == '1', pv=r['pilotView'] == '1'))
    out = []
    for i in range(1, len(frames)):
        p, q = frames[i - 1], frames[i]
        same_subject = p['held'] == q['held']
        look = angle(p['f'], q['f'])
        roll = roll_change(p['f'], p['u'], q['f'], q['u'])
        # Against the hull followed; when the hull followed changes, against the scene instead (in the new one's radii).
        move = norm(sub(q['rel'], p['rel'])) / q['rad'] if same_subject else norm(sub(q['c'], p['c'])) / q['rad']
        out.append(dict(i=i, step=q['step'], dt=q['dt'], look=look, roll=roll, move=move, dfov=q['fov'] - p['fov'],
                        mode=q['mode'], held=q['held'], show=q['show'], pv=q['pv'], fmode=p['mode'] + '>' + q['mode'] if p['mode'] != q['mode'] else q['mode']))

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
        print(f'{label}: {len(found)}')
        for o, med in found[: (999 if show_all else 25)]:
            print(f'  frame {o["i"]:5d} [{o["step"]}] {o[key]:.3f} vs median {med:.3f}  mode {o["fmode"]} held {o["held"]}'
                  f'{" show" if o["show"] else ""}{" pilotView" if o["pv"] else ""}')
    fovj = [o for o in out if abs(o['dfov']) > 1.0]
    print(f'fov jumps > 1 deg/frame: {len(fovj)}')
    for o in fovj[:25]:
        print(f'  frame {o["i"]:5d} [{o["step"]}] {o["dfov"]:+.2f}')
    return 0


if __name__ == '__main__':
    sys.exit(main())

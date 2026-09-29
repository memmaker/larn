#!/usr/bin/env python3
"""Synthesize Larn's sound effects at build time: one <event>.wav per SOUND("event")
in the game (*.c, asserted) into <out>, plus <out>/sounds.json {event: [file]}.
Larn has no sounds of its own (none upstream, none on larn.org), so these are made
for it: square-wave blips in the spirit of a 1986 terminal game. Stdlib only.
Usage (repo root): python3 web/mksounds.py <out>"""
import glob, json, math, os, random, re, struct, sys, wave

R = 22050
rnd = random.Random(1986)

def tone(f0, f1, dur, vol=.4, dec=2.0, w='sq'):
    out, ph, n = [], 0.0, int(R * dur)
    for i in range(n):
        t = i / n
        ph += f0 * (f1 / f0) ** t / R
        x = (1 if ph % 1 < .5 else -1) if w == 'sq' else math.sin(2 * math.pi * ph)
        out.append(x * vol * (1 - t) ** dec)
    return out

def noise(dur, vol=.5, dec=2.0, lp=.3, swell=False):
    out, y, n = [], 0.0, int(R * dur)
    for i in range(n):
        t = i / n
        y += lp * (rnd.uniform(-1, 1) - y)          # one-pole low-pass: small lp = duller
        out.append(y * vol * (math.sin(math.pi * t) if swell else (1 - t) ** dec))
    return out

def mix(*parts):
    out = [0.0] * max(map(len, parts))
    for p in parts:
        for i, x in enumerate(p):
            out[i] += x
    return out

def notes(fs, d=.07, **k):
    return sum((tone(f, f, d, **k) for f in fs), [])

SOUNDS = {
    'hit':         lambda: mix(noise(.09, .7, 3, .35), tone(220, 70, .09, .3)),
    'miss':        lambda: noise(.16, .35, lp=.6, swell=True),
    'kill':        lambda: mix(noise(.12, .5, 2, .2), tone(330, 55, .3, .3, 1)),
    'mon_hit':     lambda: mix(noise(.13, .8, 2, .12), tone(140, 45, .12, .35)),
    'money1':      lambda: notes([988, 1319], .06, vol=.25, dec=.6) + tone(1319, 1319, .15, .2),
    'level':       lambda: notes([523, 659, 784], .08, vol=.3, dec=.3) + tone(1047, 1047, .3, .3, 1.5),
    'quaff':       lambda: sum((tone(f, f * 1.8, .05, .3, 1, 'sin') for f in (300, 380, 340, 460)), []),
    'study':       lambda: noise(.12, .3, lp=.7, swell=True) + noise(.1, .25, lp=.8, swell=True),
    'eat':         lambda: sum((noise(.06, .6, 3, .25) + [0.0] * 1500 for _ in range(3)), []),
    'teleport':    lambda: mix(tone(200, 2400, .35, .25, .5), tone(203, 2430, .35, .15, .5, 'sin')),
    'drop':        lambda: mix(noise(.07, .6, 4, .1), tone(160, 90, .07, .3, 3)),
    'wield':       lambda: mix(tone(1800, 1750, .3, .2, 3, 'sin'), tone(2650, 2600, .3, .12, 4, 'sin'), noise(.03, .3, 4, .8)),
    'stairs_up':   lambda: notes([392, 440, 494, 523], .06, vol=.25, dec=.5),
    'stairs_down': lambda: notes([523, 494, 440, 392], .06, vol=.25, dec=.5),
    'cast_spell':  lambda: notes([880, 1175, 1480, 1760, 2349], .035, vol=.2, dec=.4) + tone(2349, 1175, .2, .15, 1.5, 'sin'),
    'store5':      lambda: tone(1568, 1568, .08, .25, 1) + tone(2093, 2093, .35, .25, 2),
    'death':       lambda: notes([392, 370, 349], .22, vol=.3, dec=.3) + tone(330, 165, .8, .3, 1.2),
}

events = set()
for f in glob.glob('*.c'):
    events |= set(re.findall(r'SOUND\s*\("(\w+)"', open(f, encoding='latin-1').read()))
assert events and events <= set(SOUNDS), 'events without a sound: %s' % sorted(events - set(SOUNDS))

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for ev in sorted(events):
    s = SOUNDS[ev]()
    with wave.open(os.path.join(out, ev + '.wav'), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(R)
        w.writeframes(b''.join(struct.pack('<h', int(max(-1, min(1, x)) * 32000)) for x in s))
json.dump({ev: [ev + '.wav'] for ev in sorted(events)}, open(os.path.join(out, 'sounds.json'), 'w'))

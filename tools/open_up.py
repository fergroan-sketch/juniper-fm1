# tools/open_up.py BANK_IN BANK_OUT: 0.5's pass over the factory bank, so no patch is a blip. It reads each patch's
# envelope and filter in seconds and hertz (the engine's curves, juno.c env_time and the cutoff) and lifts, never
# lowers, the decay, release, attack and cutoff to each kind's floor. Rows: tools/bank.txt's format.
import math, sys

def t_of(v, top): return 0.0015 * 2 ** (v / 127 * math.log2(top / 0.0015))
def v_of(t, top): return min(127, math.ceil(127 * math.log2(t / 0.0015) / math.log2(top / 0.0015)))
def oct_of(hz): return math.log2(hz / 5)

# by slot: (kind, decay floor for a patch that dies away, release, base cutoff Hz, cutoff Hz while held, the note
# it is judged at; a sustained patch's decay floor is 0.3 s)
KIND = [("init", 0, 0), ("pad", 1, 13), ("arp", 14, 26), ("bass", 27, 39), ("brass", 40, 52), ("sub", 53, 64),
        ("lead", 65, 77), ("string", 78, 90), ("pluck", 91, 103), ("stab", 104, 115), ("chord", 116, 127)]
FLOOR = {  #        D     R     base  held  note
    "pad":    (1.20, 1.20, 250,  600, 60),
    "arp":    (0.35, 0.25, 300,  500, 60),
    "bass":   (0.45, 0.18, 160,  280, 36),
    "brass":  (0.50, 0.40, 220,  900, 60),
    "sub":    (0.45, 0.20, 160,  180, 36),
    "lead":   (0.50, 0.35, 350,  900, 60),
    "string": (0.60, 0.60, 300,  900, 60),
    "pluck":  (0.90, 0.50, 380,  380, 60),
    "stab":   (0.50, 0.35, 320,  320, 60),
    "chord":  (0.60, 0.45, 320,  800, 60),
}
OWN = {  # patches whose nature asks more (slot name: decay, release)
    "PIZZICATO": (0.55, 0.40), "LONG PLUCK": (2.2, 1.2), "BELL PLUCK": (2.5, 1.5), "HARP": (1.6, 1.0),
    "GLASS PLUCK": (1.4, 0.9), "MALLET": (0.9, 0.5), "ARP BELL": (0.6, 0.4), "ARP GLASS": (0.5, 0.35),
    "DUB STAB": (0.7, 0.7), "SUB BOOM": (0.9, 0.3), "ZAP": (0.45, 0.30), "ARP ZAP": (0.3, 0.2),
}
LEVEL = {"PURE SUB": 86, "SINE SUB": 85, "SOFT SUB": 88}   # opened, these were 6-7 dB over the bank: brought back
SLOW_A = {"brass": 0.025}                  # a brass patch speaks, it does not click

def kind(i):
    for k, a, b in KIND:
        if a <= i <= b:
            return k

out = []
for i, l in enumerate(l for l in open(sys.argv[1]) if l.strip()):
    f = l.strip().split("|")
    p = list(map(int, f[1].split(",")))
    k = kind(i)
    if k != "init":
        dmin, rmin, base_hz, held_hz, note = FLOOR[k]
        dmin, rmin = OWN.get(f[0], (dmin, rmin))
        S = p[21]
        if p[17] == 0:                     # an envelope VCA: the envelope is the sound's length
            if S < 60:
                p[20] = max(p[20], v_of(dmin, 12.0))
            else:
                p[20] = max(p[20], v_of(0.3, 12.0))
            p[22] = max(p[22], v_of(rmin, 12.0))
        if k in SLOW_A:
            p[19] = max(p[19], v_of(SLOW_A[k], 3.0))
        sign = -1 if p[14] else 1
        kyb = p[16] / 127 * (note - 60) / 12
        base = p[11] / 127 * 13.3 + kyb
        held = base + sign * p[13] / 127 * 14 * S / 127
        need = max(oct_of(base_hz) - base, oct_of(held_hz) - held, 0)
        p[11] = min(127, p[11] + math.ceil(need * 127 / 13.3))
    if f[0] in LEVEL:
        p[18] = LEVEL[f[0]]
    f[1] = ",".join(map(str, p))
    out.append("|".join(f))
open(sys.argv[2], "w").write("\n".join(out) + "\n")

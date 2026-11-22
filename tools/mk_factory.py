import sys
H = ["/* SPDX-License-Identifier: MPL-2.0 */",
     "/* JUNIPER FM-1's factory bank, written by tools/gen_factory.py: designed for this firmware, none copied from any",
     " * instrument. A01 INIT; then pads, arps, bass, brass, sub, leads, strings, plucks, stabs, chords. */",
     "/*   rate dly rng lfo pwm src saw pls sub noi hpf frq res env pol vlf kyb vca lvl  A   D   S   R  cho */"]
for l in open(sys.argv[1]):
    l = l.strip()
    if not l: continue
    n, p, m, ch, a, ar = l.split("|")
    m = m.split(",")
    H.append("{{%s}, %-14s %s, %s, %s, %s, {%s}, %s, {%s}}," % (", ".join(p.split(",")), '"%s",' % n, m[0], m[1], m[2], m[3],
             ", ".join(ch.split(",")), a, ", ".join(ar.split(","))))
open(sys.argv[2], "w").write("\n".join(H) + "\n")

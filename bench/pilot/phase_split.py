"""Phase split and top timevars of the R time-report builds (GCC 16 -ftime-report, wall only).

Usage: python -I phase_split.py NAME=dir ...
"""
import os
import re
import sys

pat = re.compile(r"^\s*(.+?)\s*:\s*(\d+\.\d+)\s*(?:\(\s*\d+%\))?\s+(\d+[kMG]?)")
runs = [a.split("=", 1) for a in sys.argv[1:]]
for cell in ("many_1024", "many_2048", "wide_1024"):
    print("==", cell)
    for name, d in runs:
        p = os.path.join(d, f"R_{cell}_0.time-report.log")
        if not os.path.exists(p):
            continue
        vals = {}
        for line in open(p, encoding="utf-8", errors="replace"):
            m = pat.match(line)
            if m:
                vals[m.group(1)] = (float(m.group(2)), m.group(3))
        tot = vals["TOTAL"][0]
        top = sorted(((v[0], k, v[1]) for k, v in vals.items() if k != "TOTAL" and not k.startswith("phase")), reverse=True)[:6]
        ph = ", ".join(f"{k[6:]} {v[0]:.1f}" for k, v in vals.items() if k.startswith("phase") and v[0] >= 0.3)
        print(f"  {name}: TOTAL {tot:.1f} ({vals['TOTAL'][1]}); {ph}")
        print("     top: " + "; ".join(f"{k} {s:.1f} {mm}" for s, k, mm in top))

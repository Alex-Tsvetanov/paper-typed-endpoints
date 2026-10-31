"""cycles:u per request and shares by object and symbol group, from perf report text and server logs.

Usage: python -I perf_summary.py NAME=outdir ...
"""
import os
import re
import sys

ev = re.compile(r"Event count \(approx\.\): (\d+)")
row = re.compile(r"^\s+([\d.]+)%\s+(\S+)\s+\[\.\]\s+(.*)$")
calls = re.compile(r"calls ok (\d+)")


def group(obj, sym):
    if obj != "tep_server":
        return obj
    if sym.startswith("nghttp2") or sym in ("bubble_down", "stream_less") or "nghttp2" in sym:
        return "nghttp2"
    if "google::protobuf" in sym or "absl" in sym or "e2e::" in sym:
        return "protobuf"
    if "tep::" in sym:
        return "tep"
    return "other"


for arg in sys.argv[1:]:
    name, d = arg.split("=", 1)
    for arm in ("R", "Q"):
        for p in ("small", "medium"):
            t = open(os.path.join(d, f"perf-{arm}-{p}.txt"), encoding="utf-8", errors="replace").read()
            n = int(ev.search(t).group(1))
            log = open(os.path.join(d, f"profile-server-{arm}-{p}.log"), encoding="utf-8", errors="replace").read()
            req = int(calls.findall(log)[-1])
            g = {}
            for line in t.splitlines():
                m = row.match(line)
                if m:
                    k = group(m.group(2), m.group(3).strip())
                    g[k] = g.get(k, 0.0) + float(m.group(1))
            cpr = n / req
            parts = ", ".join(f"{k} {v:.1f}% ({v / 100 * cpr:.0f})" for k, v in sorted(g.items(), key=lambda x: -x[1]) if v >= 1)
            print(f"{name} {arm} {p:6}: {cpr:7.0f} cycles/req ({req} req); {parts}")

# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""results/macros.tex and the printed report, from analyse.py's summary. Every number in the paper
comes from these macros (hypotheses.md section 5). Macro names carry no digits (LaTeX), so sizes
are words: \\CompLinuxWideSixteenRatio is the Linux R/P ratio of wide at 16 fields.

Names: Comp<OS><Shape><N><Field> per compile cell; Grow<OS><Gen><Shape><N><Field> from N to 2N;
E<OS><Payload><Load>Rvs<Arm><Field> per end-to-end contrast (Load: Light is c1 m1, Heavy c12 m128);
E<OS><Payload><Arm><Field> per arm; Cross<Field> for the Linux ghz cross-check; Alloc<Arm><Case>;
Ops<Shape><N>. A value that does not exist (a descriptive cell's p) is "--".
"""
from __future__ import annotations

from fractions import Fraction

import stats as S

WORDS = {16: "Sixteen", 32: "ThirtyTwo", 64: "SixtyFour", 128: "OneTwentyEight", 256: "TwoFiftySix",
         512: "FiveTwelve", 1024: "TenTwentyFour", 2048: "TwentyFortyEight", 4096: "FortyNinetySix"}
NONE = "--"


def cap(word: str) -> str:
    return word[:1].upper() + word[1:]


def ratio(x) -> str:
    return NONE if x is None else f"{x:.3f}"


def secs(x) -> str:
    return NONE if x is None else f"{x:.2f}"


def one(x) -> str:
    return NONE if x is None else f"{x:.1f}"


def whole(x) -> str:
    return NONE if x is None else f"{round(x):,}".replace(",", "{,}")


def pval(p) -> str:
    if p is None:
        return NONE
    return "1" if Fraction(p) == 1 else f"{float(p):.2g}"


def yes(x) -> str:
    return NONE if x is None else ("yes" if x else "no")


def secs3(x) -> str:
    return NONE if x is None else f"{x:.3f}"


# compile_curve.py's phases, in build order (hypotheses.md section 2).
PHASES = {"R": ("compile", "link"), "P": ("protoc", "compile_pb", "compile_user", "link")}


def phase_macros(summary: dict) -> list[tuple[str, str]]:
    """Added 2026-11-03 (design/revision-log.md): each phase's median wall time per cell and generator,
    in seconds (Comp<OS><Shape><N>Phase<Gen><Phase>). render() appends them after every earlier
    macro, so the earlier macros.tex is a prefix of the new one."""
    out = []
    for c in summary["compile"]["cells"]:
        base = f"Comp{cap(c['os'])}{cap(c['shape'])}{WORDS[c['n']]}Phase"
        for gen, key in (("R", "phases_r_s"), ("P", "phases_p_s")):
            phases = c.get(key) or {}
            if phases and set(phases) != set(PHASES[gen]):  # compile_curve.py's phases changed: say so, print no "--"
                raise ValueError(f"{base} {gen}: phases {sorted(phases)}, want {sorted(PHASES[gen])}")
            out += [(base + gen + "".join(cap(w) for w in name.split("_")), secs3(phases.get(name)))
                    for name in PHASES[gen]]
    return out


def compile_macros(summary: dict) -> list[tuple[str, str]]:
    out = []
    for c in summary["compile"]["cells"]:
        base = f"Comp{cap(c['os'])}{cap(c['shape'])}{WORDS[c['n']]}"
        out += [(base + "Ratio", ratio(c["ratio"])), (base + "Low", ratio(c["low"])), (base + "High", ratio(c["high"])),
                (base + "MedR", secs(c.get("median_r_s"))), (base + "MedP", secs(c.get("median_p_s"))),
                (base + "PeakR", whole(c.get("peak_r_mib"))), (base + "PeakP", whole(c.get("peak_p_mib"))),
                (base + "Pairs", str(c["pairs"])), (base + "InDir", str(c.get("in_direction", NONE))),
                (base + "P", pval(c.get("p"))), (base + "Pass", yes(c.get("holm_pass")))]
    for g in summary["compile"]["growth"]:
        base = f"Grow{cap(g['os'])}{g['gen']}{cap(g['shape'])}{WORDS[g['n']]}"
        out += [(base + "Ratio", ratio(g["ratio"])), (base + "Low", ratio(g["low"])), (base + "High", ratio(g["high"]))]
    return out


def e2e_macros(summary: dict) -> list[tuple[str, str]]:
    out = []
    for c in summary["e2e"]["contrasts"]:
        base = f"E{cap(c['os'])}{cap(c['payload'])}{cap(c['load'])}Rvs{c['other']}"
        out += [(base + "Ratio", ratio(c["ratio"])), (base + "Low", ratio(c["low"])), (base + "High", ratio(c["high"])),
                (base + "Reps", str(c["replicates"])), (base + "InDir", str(c.get("in_direction", NONE))),
                (base + "P", pval(c.get("p"))), (base + "Pass", yes(c.get("holm_pass")))]
    for a in summary["e2e"]["arms"]:
        base = f"E{cap(a['os'])}{cap(a['payload'])}{a['arm']}"
        out += [(base + "LightRps", whole(a["light_rps"])), (base + "HeavyRps", whole(a["heavy_rps"])),
                (base + "LightSharePct", one(a["light_server_share"])), (base + "HeavySharePct", one(a["heavy_server_share"])),
                (base + "LightGenSharePct", one(a["light_generator_share"])),
                (base + "HeavyGenSharePct", one(a["heavy_generator_share"])), (base + "Peak", one(a["peak_mib"]))]
        if a["os"] == "linux":
            out += [(base + "LatMed", one(a["light_latency_median_us"])), (base + "LatNinetyNine", one(a["light_latency_p99_us"]))]
        else:
            out.append((base + "Cycle", one(a["light_cycle_us"])))
    x = summary["e2e"]["cross_check"]
    out += [("CrossR", whole(x["R"])), ("CrossP", whole(x["P"])), ("CrossRvsP", ratio(x["ratio"])),
            ("CrossFailedWindows", str(len(x["failed_windows"])))]
    return out


def render(summary: dict) -> str:
    seeds = [(f"SeedOrder{word}{cap(o)}", str(S.seed_of(f"P5 campaign {e} {o}")))  # an identifier, printed as the order file does
             for e, word in (("compile", "Compile"), ("e2e", "EndToEnd")) for o in ("linux", "windows")]
    rows = [("Alpha", f"{summary['alpha']}"), ("Resamples", whole(summary["resamples"])),
            ("CompM", str(summary["compile"]["m"])), ("EM", str(summary["e2e"]["m"]))] + seeds
    rows += compile_macros(summary) + e2e_macros(summary)
    if summary.get("allocs"):
        rows += [(f"Alloc{k.split()[0]}{cap(k.split()[1])}", one(v)) for k, v in summary["allocs"].items()]
    if summary.get("constexpr_ops"):
        rows += [(f"Ops{cap(k.split()[0])}{WORDS[int(k.split()[1])]}", whole(v)) for k, v in summary["constexpr_ops"].items()]
    rows += phase_macros(summary)
    names = [n for n, _ in rows]
    if len(names) != len(set(names)):
        raise ValueError("two macros share a name")
    head = "% Generated by analysis/analyse.py (analysis/macros.py). Do not edit.\n"
    return head + "".join(f"\\newcommand{{\\{n}}}{{{v}}}\n" for n, v in rows)


def compile_table(summary: dict) -> str:
    """results/tables/compile.tex (added 2026-11-03, design/revision-log.md): one row per compile cell,
    Linux then Windows, wide then many, by N. Rows only; every value is a macro of render()'s output,
    so the table repeats no number. A descriptive cell prints "no test" instead of its "--" macros,
    and the secondary curve's N carries a dagger."""
    order = {(os_name, shape): i for i, (os_name, shape) in
             enumerate((o, s) for o in ("linux", "windows") for s in ("wide", "many"))}
    rows = []
    for c in sorted(summary["compile"]["cells"], key=lambda c: (order[(c["os"], c["shape"])], c["n"])):
        m = f"\\Comp{cap(c['os'])}{cap(c['shape'])}{WORDS[c['n']]}"
        n = f"{c['n']}$^{{\\dagger}}$" if c["secondary"] else str(c["n"])
        test = (f"{c['direction']} & {m}InDir{{}}/{m}Pairs{{}} & {m}Pass{{}}" if c["direction"]
                else "no test & no test & no test")
        rows.append(f"{cap(c['os'])} & {'wide' if c['shape'] == 'wide' else 'many-message'} & {n} & "
                    f"{m}MedR{{}} & {m}MedP{{}} & {m}Ratio{{}} & [{m}Low{{}}, {m}High{{}}] & {test} & "
                    f"{m}PeakR{{}} & {m}PeakP{{}} \\\\\n")
    return "% Generated by analysis/analyse.py (analysis/macros.py). Do not edit.\n" + "".join(rows)


def growth_table(summary: dict) -> str:
    """results/tables/growth.tex (added 2026-11-03, design/revision-log.md): one row per OS, shape and
    N from 16 to 2048, Linux then Windows, wide then many: each generator's growth from N to 2N with
    its interval, every value a macro. A row whose reflection value ends at the secondary curve's
    build carries a dagger; a growth that could not be computed prints "not computed"."""
    keyed = {(g["os"], g["shape"], g["n"], g["gen"]): g for g in summary["compile"]["growth"]}
    rows = []
    for os_name in ("linux", "windows"):
        for shape in ("wide", "many"):
            for n in sorted({n for (o, s, n, _) in keyed if (o, s) == (os_name, shape)}):
                cells = []
                for gen in ("R", "P"):
                    g = keyed.get((os_name, shape, n, gen))
                    m = f"\\Grow{cap(os_name)}{gen}{cap(shape)}{WORDS[n]}"
                    cells.append("not computed & not computed" if g is None or g["ratio"] is None
                                 else f"{m}Ratio{{}} & [{m}Low{{}}, {m}High{{}}]")
                r = keyed.get((os_name, shape, n, "R"))
                mark = "$^{\\dagger}$" if r is not None and r.get("secondary") else ""
                rows.append(f"{cap(os_name)} & {'wide' if shape == 'wide' else 'many-message'} & {n} to {2 * n}{mark} & "
                            f"{cells[0]} & {cells[1]} \\\\\n")
    return "% Generated by analysis/analyse.py (analysis/macros.py). Do not edit.\n" + "".join(rows)


def report(summary: dict) -> str:
    lines = [f"Python {summary['python']}; alpha {summary['alpha']}; {summary['resamples']:,} resamples"]
    comp = summary["compile"]
    lines.append(f"compile: m = {comp['m']} (section 7: 36); void pairs {comp['void']}; descriptive {comp['descriptive']}")
    for c in comp["cells"]:
        test = (f"{c['direction']} {c['in_direction']}/{c['pairs']} p {pval(c['p'])} Holm {yes(c['holm_pass'])}"
                if c["direction"] else "descriptive")
        label = "  secondary curve" if c["secondary"] else ""
        lines.append(f"  {c['os']:7} {c['shape']:4} {c['n']:5} R/P {ratio(c['ratio'])} [{ratio(c['low'])}, {ratio(c['high'])}]  {test}{label}")
    e = summary["e2e"]
    lines.append(f"end to end: m = {e['m']} (section 7: 7); void runs {e['void']}; payloads touched by a run void twice {e['touched']}")
    for c in e["contrasts"]:
        test = (f"{c['in_direction']}/{c['replicates']} p {pval(c['p'])} Holm {yes(c['holm_pass'])}"
                if c["hypothesis"] else "descriptive")
        lines.append(f"  {c['os']:7} {c['payload']:6} {c['load']:5} R/{c['other']} {ratio(c['ratio'])} "
                     f"[{ratio(c['low'])}, {ratio(c['high'])}]  {test}")
    x = e["cross_check"]
    lines.append(f"cross-check (Linux, ghz, medium c12 m128): R {whole(x['R'])}, P {whole(x['P'])}, R/P {ratio(x['ratio'])}, "
                 f"failed or missing windows {x['failed_windows']}")
    return "\n".join(lines)

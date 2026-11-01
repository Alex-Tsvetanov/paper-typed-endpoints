# Copyright 2026 Alex Tsvetanov
# SPDX-License-Identifier: Apache-2.0
"""The statistics of hypotheses.md (frozen at 704061a), sections 2 and 3: the exact one-sided sign
test, Holm's step-down at family-wise alpha 0.05, and the 95% BCa interval of a ratio from a
clustered bootstrap with 10,000 resamples. Standard library only (Python 3.14).

Conventions, adapted from the author's earlier papers' analysis/stats.py (typed-routing, one-port):
z0 counts resamples equal to the estimate as half and clips the share to [1/(2B), 1 - 1/(2B)]; the
acceleration comes from the jackknife over clusters, a = sum(d^3) / (6 sum(d^2)^1.5) with
d = mean(jk) - jk, and 0 when every jackknife value is equal; the interval's ends are order
statistics of the resamples at the BCa-adjusted levels, the inverse of their empirical
distribution (NumPy's "inverted_cdf"). A cluster is what the hypotheses resample: a build pair
(section 2) or a replicate with all its arms (section 3). Each bootstrap seeds its own
random.Random from sha256 of a label naming the cell (seed_of), so an interval does not depend on
the order in which cells are analysed; a resample draws its K cluster indices with randrange(K).
"""
from __future__ import annotations

import hashlib
import math
import random
import statistics
from dataclasses import dataclass
from fractions import Fraction
from typing import Callable, Sequence, TypeVar

ALPHA = 0.05  # hypotheses.md sections 2 and 3: Holm at alpha 0.05 family-wise, one-sided tests
B_RESAMPLES = 10_000  # sections 2 and 3: 10,000 resamples
LEVEL = 0.95  # sections 2 and 3: 95% BCa intervals

_N = statistics.NormalDist()
T = TypeVar("T")


def seed_of(label: str) -> int:
    """The first 8 hex digits of sha256(label), as an integer (as bench/campaign_order.py)."""
    return int(hashlib.sha256(label.encode("ascii")).hexdigest()[:8], 16)


def sign_p(x: int, n: int) -> Fraction:
    """P(X >= x) for X binomial with n trials and 1/2, exactly: the one-sided sign test's p for x
    of n pairs or replicates in the claimed direction (a tie counts against)."""
    if n < 0:
        raise ValueError("n < 0")
    return Fraction(sum(math.comb(n, k) for k in range(max(int(x), 0), n + 1)), 2 ** n)


def holm_adjust(pvals: Sequence[float]) -> list[float]:
    """Holm's adjusted p-values, in the order given: sorted ascending, the i-th smallest (from 0)
    times (m - i), made monotone, capped at 1."""
    m = len(pvals)
    order = sorted(range(m), key=lambda i: (pvals[i], i))
    adjusted = [0.0] * m
    running = 0.0
    for rank, i in enumerate(order):
        running = max(running, min(1.0, (m - rank) * float(pvals[i])))
        adjusted[i] = running
    return adjusted


def holm_pass(pvals: Sequence[float], alpha: float = ALPHA) -> list[bool]:
    """Holm's step-down at family-wise alpha: a hypothesis passes when its adjusted p is at most alpha."""
    return [a <= alpha for a in holm_adjust(pvals)]


@dataclass(frozen=True)
class Interval:
    """A statistic, its two-sided BCa interval (None where the BCa map is degenerate), and how it was made."""
    theta: float
    low: float | None
    high: float | None
    z0: float
    a: float
    clusters: int
    resamples: int
    seed: int


def inverted_cdf(sorted_values: Sequence[float], q: float) -> float:
    """The q-quantile as the inverse of the empirical distribution: the smallest value whose share of
    values at or below it is at least q (NumPy's method "inverted_cdf")."""
    n = len(sorted_values)
    j = min(max(math.ceil(n * q) - 1, 0), n - 1)
    return sorted_values[j]


def bca(clusters: Sequence[T], stat: Callable[[Sequence[T]], float], label: str,
        resamples: int = B_RESAMPLES, level: float = LEVEL) -> Interval:
    """The BCa interval of stat over the clusters, resampled with replacement, seeded by seed_of(label)."""
    k = len(clusters)
    if k < 2:
        raise ValueError("a bootstrap needs at least two clusters")
    theta = stat(clusters)
    seed = seed_of(label)
    rng = random.Random(seed)
    theta_b = [stat([clusters[rng.randrange(k)] for _ in range(k)]) for _ in range(resamples)]
    share = (sum(t < theta for t in theta_b) + 0.5 * sum(t == theta for t in theta_b)) / resamples
    share = min(max(share, 1.0 / (2 * resamples)), 1.0 - 1.0 / (2 * resamples))
    z0 = _N.inv_cdf(share)
    jk = [stat(list(clusters[:i]) + list(clusters[i + 1:])) for i in range(k)]
    mean_jk = statistics.fmean(jk)
    d = [mean_jk - v for v in jk]
    den = sum(x * x for x in d)
    a = sum(x ** 3 for x in d) / (6 * den ** 1.5) if den > 0 else 0.0
    theta_b.sort()
    tail = (1.0 - level) / 2.0
    ends: list[float | None] = []
    for z in (_N.inv_cdf(tail), _N.inv_cdf(1.0 - tail)):
        t = z0 + z
        scale = 1.0 - a * t
        ends.append(None if scale <= 0 else inverted_cdf(theta_b, _N.cdf(z0 + t / scale)))
    return Interval(theta, ends[0], ends[1], z0, a, k, resamples, seed)


def ratio_of_medians(pairs: Sequence[tuple[float, float]]) -> float:
    """median of the first members over median of the second (section 2: R/P, and growth 2N/N)."""
    return statistics.median(p[0] for p in pairs) / statistics.median(p[1] for p in pairs)


def ratio_of_means(pairs: Sequence[tuple[float, float]]) -> float:
    """mean of the first members over mean of the second (section 3: R against P or Q)."""
    return statistics.fmean(p[0] for p in pairs) / statistics.fmean(p[1] for p in pairs)

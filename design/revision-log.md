# P5 revision log

Changes to the design after a decision, newest last. Each entry names who decided, what changed,
and the measurement it rests on. Frozen files are never edited; their changes are entries here.

## 2026-10-30. `many` at 4096 fields is a secondary-curve cell

Decided by the coordinator (2026-10-30, under design note section 4, which allows a declared
secondary curve; Alex was not asked).

- The reflection generator compiles `many` 4096 only with a raised constant-evaluation limit.
  Its schema tables need 35.7M operations (`bench/constexpr_ops.sh many 4096 tables`, GCC 16.2.0,
  commit d7d7ab8, deterministic count, not a timing); GCC's default limit is 33,554,432. The count
  is linear in the field count: 8.91M at 1024, 17.8M at 2048.
- The cell is built with `-fconstexpr-ops-limit=67108864` (twice the default) and reported on the
  secondary curve, with that limit and the measured count stated in the paper.
- Every other cell of the ladder (both shapes from 16 to 2048 fields, `wide` at 4096) compiles
  under the default limits and is on the primary curve.
- Not done: moving tokenization into a separately evaluated stage, which would bring the cell
  under the default limit (about 25M) without shortening the compile.

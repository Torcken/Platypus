# Phase 2: Reliability with small models

The old plan for generic multi-file editing did not match the canonical roadmap and is superseded.

1. Generate GBNF per syntax family and flat tool schema; preserve the tolerant fallback.
2. Add regressions from real Phase 1 recordings.
3. Implement ranked tree-sitter maps for the six specified language families after the stable prefix.
4. Enforce per-tier token budgets and compact only at task boundaries.
5. Extend the existing prefix-stability test with runtime diagnostics.
6. Detect fourth-and-later identical actions and inject a corrective message.

Test each module offline. Promote model-visible changes only after paired real-model evidence:
invalid calls <1%, success +10 percentage points, prompt tokens -30% with supporting intervals.
No Phase 2 delivery or measured improvement is claimed by the stabilization milestone.

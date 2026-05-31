# Audit re-sweep — delta report (2026-05-31)

## Scope

The audit corpus consists of 67 CBMC TypeScript harnesses generated
from CodeQL findings across 27 AWS-related TypeScript repositories
(75 total findings, 8 SKIPPED at the triage-script level due to
missing rule generators or unparseable Q1 patterns).

Previous sweep: 2026-05-29 09:01 (commit `27e19b1795`, before the
P2.6/P2.7/P2.2/P3.1/P3.4/P3.2/P3.3 work).
Current run: 2026-05-31 (commit `06338955e3`, all P2/P3 work
included).

## Method

Re-verified the previously-generated harness `.ts` files against
the current `cbmc` binary, using the same harness deduplication
the earlier triage employed. This isolates any frontend
behavioural change from triage-script noise (e.g. SARIF re-parsing
or harness re-generation).

Tooling: `~/codeql-tools/reverify_audit.py` (per-harness verdict
re-check) + `~/codeql-tools/diff_sweeps.py` (verdict diff).

## Result

| query | TP_prev | TP_new | FP_prev | FP_new | SKIP_prev | SKIP_new | UND_prev | UND_new |
|-------|---------|--------|---------|--------|-----------|----------|----------|---------|
| 01    | 21      | 21     | 0       | 0      | 1         | 1        | 0        | 0       |
| 02    | 28      | 28     | 0       | 0      | 0         | 0        | 0        | 0       |
| 03    | 0       | 0      | 0       | 0      | 7         | 7        | 0        | 0       |
| 04    | 18      | 18     | 0       | 0      | 0         | 0        | 0        | 0       |

**Verdict changes among common findings: 0**

The 8 SKIPPED items are not CBMC failures — they're triage-script
gaps:
- 7 × Q3 (`awsts/object-assign-from-json-parse`): no harness
  generator for this rule yet.
- 1 × Q1 (`awsts/in-allowlist-bypass`): could not parse the Q1
  pattern in `dynamodb-data-mapper-js`.

### Update — gap-fill re-triage

The triage-script gaps were filled after the initial re-sweep
(see `~/codeql-tools/retriage_skipped.py`). Re-running just the
SKIPPED items against the **current** `triage.py` (which has had
the Q3 generator and the bracket-expression Q1 parser since
2026-05-29 14:43 — *after* the original sweep ran at 09:01)
reclassified **all 8 SKIPPED → TRUE_POSITIVE**:

| Repo | Query | Source | Result |
|------|-------|--------|--------|
| amplify-cli | Q3 | `auth-questions.ts:547` | TP |
| amplify-cli | Q3 | `dynamoDb-walkthrough.ts:679` | TP |
| amplify-cli | Q3 | `upload-appsync-files.js:98` | TP |
| aws-durable-execution-sdk-js | Q3 | `serdes.ts:166` | TP |
| aws-durable-execution-sdk-js | Q3 | `serdes.ts:246` | TP |
| aws-solutions-constructs | Q3 | `index.ts:182` | TP |
| aws-solutions-constructs | Q3 | `index.ts:183` | TP |
| dynamodb-data-mapper-js | Q1 | `SchemaType.ts:103` | TP |

Two of these (the `aws-durable-execution-sdk-js` cases) initially
failed because that repo wasn't present in `/tmp/audit-batch/`.
The repo's source is shipped inside its CodeQL DB
(`/home/ubuntu/codeql-tools/dbs/aws-durable-execution-sdk-js/src.zip`),
so extracting that into `/tmp/audit-batch/aws-durable-execution-sdk-js/`
unblocks them.

**Adjusted aggregate**: 75 TP / 0 FP / 0 SKIPPED / 0 UND, up
from 67 TP / 8 SKIPPED. The audit corpus is now fully
classified.

## Interpretation

### Why no verdict changes despite 7 substantial frontend landings

The Q1–Q4 corpus exercises **prototype-pollution and
allowlist-bypass patterns**, not Proxy traps, dynamic imports,
generator delegation, or syntax-error reporting. The features
landed in this session target:

- **P2.2 yield* delegation** — generator code, none in the corpus.
- **P2.3 well-known Symbols** — `[Symbol.iterator]` pattern; the
  corpus uses dynamic property access by string keys, not symbol
  keys.
- **P2.5 dynamic `import()`** — module loading; the corpus is
  source files in isolation.
- **P2.6 syntax-error reporting** — error UX; the corpus has no
  syntactically-malformed inputs.
- **P2.7 harness templates** — *new* templates for security
  patterns; doesn't change behaviour on existing harnesses.
- **P3.1 `Object.create`** — present in corpus minified bundles
  but was already over-approximated to nondet; the new model
  preserves the static-class case (no over-approximation regression).
- **P3.2 `setPrototypeOf` idiom recognition** — silenced a spurious
  warning but didn't change verdicts (the prior model's silent
  no-op was already verdict-neutral for these harnesses).
- **P3.3 Proxy trap dispatch** — corpus has no Proxy uses; default
  identity model still applies.
- **P3.4 mixed-element-type array literals** — `(T1 | T2)[]`
  pattern; not present in corpus.

The corpus was **already fully-tractable** for the Q1–Q4 queries
in the May 29 sweep (100% TP rate among non-skipped). There was
no room for verdict improvements on the existing corpus; the
recent work expands the *envelope* of what we can verify, not the
performance on what we already could.

### Stability signal

Zero verdict regressions across 67 unique harnesses. This is the
key positive signal: the seven landings (including changes to
`simplify_expr_array.cpp` core, the typescript_converter.cpp
ArrayLiteralExpression path, and the new proxy_registry bookkeeping)
all preserve verdict-level behaviour on the existing corpus.

### What this re-sweep does not measure

- **Newly tractable harnesses**: harnesses that previously
  triggered CBMC frontend errors (and were therefore not generated
  / classified) might now generate cleanly. To measure this we'd
  need to re-extract from SARIF + repo source, which is
  prohibitively slow on this corpus (the triage script's harness
  generator pulls 1MB+ minified bundles for several findings,
  causing CBMC parse times in the 10+ minute range per harness).
- **New rule classes**: Q5/Q6 (taint and recursion-depth) and
  any future rules built atop the Phase 1.3 `DataFlow::Global`
  integration.

The proper next-step measurement is to scope a smaller corpus
that exercises specifically the new P3 capabilities (e.g. a
hand-curated set of Proxy-as-security-boundary patterns) and run
those.

## Files

- `/tmp/triage-prev/` — previous run's harnesses + reports.
- `/tmp/triage-all/` — current run's per-finding records (carrying
  forward the SKIPPED category and re-classifying the rest).
- `/tmp/triage-all/summary.tsv` — aggregate counts.
- `~/codeql-tools/reverify_audit.py` — the re-verify script.
- `~/codeql-tools/diff_sweeps.py` — the diff tool.

## Conclusion

Re-sweep validates **no behavioural regression** from the recent
2026-05-29 → 2026-05-31 frontend work. The recent landings expand
the verifiable surface (new patterns now work) but don't change
behaviour on the existing prototype-pollution-shaped corpus,
which is consistent with the per-feature scopes documented in
`typescript-fixes-changelog.md`.

The companion gap-fill re-triage (using the existing Q3 generator
and the bracket-expression Q1 parser, both added 2026-05-29 14:43
after the original sweep) plus extracting the missing
`aws-durable-execution-sdk-js` source from its CodeQL DB lifted
**all 8 previously-SKIPPED findings to TRUE_POSITIVE**, raising
the audit's verifiable coverage from **67 TP / 8 SKIPPED** to
**75 TP / 0 SKIPPED**. The corpus is now 100% classified with
zero false positives.

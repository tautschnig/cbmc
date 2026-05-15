# Benchmark issues analysis (Python AWS SDK suite)

This document enumerates the precision gaps in the CBMC Python frontend
discovered while running the `python-verification-benchmarks` AWS SDK
suite (51 benchmarks). It is the canonical reference for ongoing work on
soundness and precision.

The benchmark command is:

```
cd ~/python-verification-benchmarks
ulimit -v 4000000
./scripts/run_tool.sh -s full -t 120 -o results.csv -- \
  $HOME/cbmc-python.git/build/bin/cbmc \
  --no-unwinding-assertions --unwind 3 --python-no-exception-checks
```

## Current bottom line

51 benchmarks. As of 2026-05-14 (after items A, D1, B2, B1
refinement, and the spread-typecast / nil-arg fixup):

| Result   | Count | Note                                       |
|----------|-------|--------------------------------------------|
| CLEAN    | 39    | clean benchmarks correctly classified      |
| TP       | 5     | buggy benchmarks correctly classified      |
| MISS     | 7     | buggy benchmarks classified as clean       |
| FP       | 0     | clean benchmarks misclassified             |
| TOERR    | 0     | all benchmarks complete with a verdict     |
| TIMEOUT  | 0     |                                            |
| OOM      | 0     |                                            |

Pass rate: **(39 + 5) / 51 = 86.3 %** with default flags.

**Under both opt-in flags** (`--python-required-kwarg-checks
--python-check-typeddict-fields`):

| Result   | Count |
|----------|-------|
| CLEAN    | 39    |
| TP       | 9     |
| MISS     | 3     |
| FP       | 0     |
| TOERR    | 0     |
| TIMEOUT  | 0     |
| OOM      | 0     |

Pass rate: **(39 + 9) / 51 = 94.1 %** with the opt-in flags.
All benchmarks now produce a definite verdict — no
TOERR/TIMEOUT/OOM with either configuration.

## Cross-backend (default vs `--cvc5`)

The numbers above are with the **default** back-end (boolbv +
string-refinement loop). For comparison, the same suite under
`--cvc5` (SMT-LIB output to CVC5 with the `String` theory):

| Backend          | CLEAN | TP | MISS | FP | TOERR | OOM | Pass-rate |
|------------------|-------|----|------|----|-------|-----|-----------|
| default          |    39 |  9 |    3 |  0 |     0 |   0 | **94.1 %** |
| `--smt2 --cvc5`  |    35 |  7 |    3 |  0 |     5 |   1 |   82.4 %  |

Under `--cvc5`, 5 benchmarks ERROR (down from 15 before
this round of cvc5-IR-compatibility work) plus 1 OOM.
Five rounds of fixes contributed:

1. **Class struct components deduplication** —
   `python_converter_defs.cpp` now tracks a
   `declared_fields` set across all four field sources
   (class-level AnnAssign / Assign, __init__ AnnAssign,
   __init__ self.attr =) and skips duplicate names.
   Fixed: `Parse Error: struct.X.field already declared
   in this datatype`. Benchmarks moved out of TOERR:
   `apigateway_key_manager` family, `s3_backup_restore`,
   `sagemaker_labeling_job`, `clear_duplicate_dynamodb_entries`,
   `s3_bucket_utils`, `kms_client_manager`,
   `websocket_url_validator`.

2. **`cprover_string_concat_func` in `smt2_conv` no longer
   emits a malformed struct constructor.** The intrinsic's
   sentinel return type was being misinterpreted by the
   smt2_conv interception, which output
   `(mk-(_ BitVec 32) ...)` — invalid SMT-LIB. Now emits
   `(_ bv0 W)`; the actual string content is enforced via
   the separately-assigned `__string_len_X` /
   `__string_ptr_X` symbols at the front-end level.

3. **`List[X]` / `Set[X]` annotation recognition** —
   convert_type_annotation now handles capitalised
   `typing` aliases (List, Set, FrozenSet) the same way
   as their lowercase counterparts. Fixed:
   `aws_resource_tagger` — caller's `list[str]` no longer
   typecasts to `list[int]` via struct-narrowing
   bit-extract.

4. **Front-end guards for opaque-struct arithmetic** —
   `Add` / `Sub` / `Mult` on opaque struct/struct_tag
   types (e.g. `datetime - timedelta` in
   `check_storage_costs`) now return a sound nondet of
   the left operand's type rather than emitting a
   `minus_exprt` that smt2_conv UNEXPECTEDCASEs on.
   Fixed: `check_storage_costs`.

5. **`float()` on a python_value** — extracts the
   `__float_val` field directly via `python_value_float`
   instead of emitting a `typecast_exprt` that
   smt2_conv's `Unknown typecast struct_tag -> float` UNEXPECTEDCASEs.
   For non-tagged-union opaque structs, returns a nondet
   float over-approximation. Fixed: `s3_to_dynamodb`.

The earlier draft's heterogeneous-dict-value promotion to
`python_value_type` was reverted: it materialised a fresh
struct value per dict entry, and benchmarks that build many
heterogeneous dicts (`cloudwatch_metrics_example`) hit
CBMC's 256-object pointer-model limit. That's a real
issue but its fix is a different feature (e.g.
`--object-bits`). Net effect: `ses_email_example` keeps
ERRORing on cvc5 in the meantime.

Remaining 5 TOERR + 1 OOM under cvc5 split into:

* **2 bit-extract on struct datatypes** (`ses_email_example`
  via dict-of-dict typecast through bit-flatten;
  `execute_stepfunction` via list-of-tagged-union-element
  vs flat-bit-vector mismatch). Root cause: CBMC's
  smt2_conv falls back to `(_ BitVec N)` when a struct's
  component is an unregistered struct type; later
  references to the same struct via a different IR path
  see it as a datatype, producing the type mismatch. The
  fix would be a pre-pass through CBMC's smt2_conv to
  ensure all struct types reachable from an emitted
  expression are pre-registered before any expression
  emission begins. Deferred.

* **1 `flatten2bv` invariant under FPA theory**
  (`ecs_utils`). CBMC's smt2_conv emits `concat` over a
  float operand when FPA theory is enabled, but the
  invariant says floats should be already flattened
  upstream when FPA is on. Deeper CBMC core bug requiring
  upstream caller analysis.

* **2 cvc5 OOM-style failures** (`apigateway_key_manager`,
  `kms_client_manager`). Formula sizes are 3-4 MB / 70k+
  lines of SMT-LIB; cvc5 ran out of memory while solving.
  Mitigation would require cutting formula size at the
  CBMC level (e.g. simplifying string-refinement axioms
  or using smaller list/dict bounds). Outside this
  round's scope.

These are tracked as future work; the default back-end
remains the production target.

Compared to the 2026-04 baseline (27 CLEAN + 1 TP + 9 MISS + 3 FP +
6 TOERR + 3 TIMEOUT + 2 OOM = 54.9 %), the rate has improved
substantially through dict-precision, idiom recognition, attribute-error
detection, and elimination of all timeouts / OOMs / tool-errors.

## Outstanding cases — TL;DR

| Benchmark                                  | Verdict | Sub-category                                          | Actionability |
|---|---|---|---|
| websocket_url_validator                    | FP      | Path-sensitive `len()`-bound idiom                    | Actionable    |
| check_storage_costs                        | MISS    | Stub-level semantic gap (per-metric dimensions)       | Stub work     |
| clear_duplicate_dynamodb_entries           | TP      | (now detected via static AttributeError mechanism)    | —             |
| create_bedrock_inference_profile           | TP      | (now detected via static AttributeError mechanism)    | —             |
| rds_instance_creator.1                     | TP      | (now detected via static AttributeError mechanism)    | —             |
| create_s3_vector_index                     | MISS    | Required-kwarg detection (gated flag)                 | Flag refinement |
| test_bedrock_guardrails                    | MISS    | Required-kwarg detection (gated flag)                 | Flag refinement |
| rds_instance_creator.2                     | MISS    | TypedDict field-type enforcement at call sites        | Actionable    |
| s3_backup_restore                          | TP      | (now detected via TypedDict B2 + for-loop body conversion) | —             |
| sagemaker_labeling_job                     | TP      | (now detected via stage 1 of the re-precision plan)   | —             |
| bedrock_data_automation_example            | MISS    | Type erasure at function boundary (`param: Any`)      | Hard          |
| mediaconvert_manager                       | MISS    | Conditional required argument (spec lacks constraint) | Stub work     |


## Detailed analysis — false positive

### websocket_url_validator (FP, 1 case)

```python
def extract_api_id_from_url(websocket_url):
    parsed = urlparse(websocket_url)
    if not parsed.hostname:
        return None
    hostname_parts = parsed.hostname.split('.')
    if len(hostname_parts) >= 3 and hostname_parts[1] == 'execute-api':
        return hostname_parts[0]
    return None
```

CBMC reports `index-out-of-bounds` at line 20 (`hostname_parts[1]`).
The `len(hostname_parts) >= 3` guard is the LEFT operand of an
`and`-expression whose RIGHT operand is the subscript. Python
short-circuits, so the subscript only executes when the length is
≥ 3, but the frontend doesn't propagate the length-bound from the
left operand to the index check on the right.

**Sub-category:** *Path-sensitive length-check idiom for index
bounds.* Symmetric to the dict `if K not in D: D[K] = ...` idiom
already implemented; needs a similar path-sensitive guard
recognition for `len(L) >= N → L[i] safe for i < N`.

**Actionable.** Estimated 60–100 lines in `convert_compare`
(detect `Compare(BoolOp(And), [Compare(Call(len, L), ≥, N), …])`)
plus a side-map consulted in `convert_subscript`.


## Detailed analysis — missing detections (MISS)

### 1. check_storage_costs

```python
cloudwatch.get_metric_statistics(
    Namespace='AWS/S3',
    MetricName='BucketSizeBytes',
    Dimensions=[{'Name': 'StorageType', 'Value': 'StandardStorage'}],
    StartTime=start_time, EndTime=end_time, Period=86400,
    Statistics=['Average'])
```

The bug: CloudWatch's `BucketSizeBytes` metric requires a
`BucketName` dimension; with only `StorageType` it fails. The
DynamoDB call (also in this benchmark) needs `TableName`.

The CloudWatch stub in `stubs-full-python/boto3/CloudWatch.py`
asserts `len(Namespace) ≥ 1`, `len(MetricName) ≥ 1`, and pattern
constraints — but does not encode the per-metric dimension
schema (e.g. "BucketSizeBytes ⇒ Dimensions must contain
BucketName"). That schema is service-/metric-specific.

**Sub-category:** *Stub-level semantic gap.* Detection would
require either richer stubs or a generic "valid argument
combinations" mechanism in the front-end. Not addressable
without stub work.

### 2. create_s3_vector_index (and test_bedrock_guardrails)

```python
s3vectors_client.create_index(indexName=index_name, dimension=768)
```

Missing `dataType` and `distanceMetric`, both declared in the
S3Vectors stub's TypedDict as `Required[Literal[...]]`.
test_bedrock_guardrails has the same shape — missing
`blockedInputMessaging` and `blockedOutputsMessaging` to
`create_guardrail`.

Detection works **with `--python-required-kwarg-checks`**, the
existing Tier-1B opt-in flag. With the flag enabled, the suite
gains 2 additional TPs but introduces 2 FPs and 1 TOERR
(glue_job_runner CLEAN→FP, kms_client_manager CLEAN→FP,
apigateway_key_manager TP→TOERR). The flag therefore stays
off-by-default.

**Sub-category:** *Required-kwarg detection.* Implemented but
not safe to enable by default. Needs FP-source elimination in
the kwarg checker before promotion.

### 3. rds_instance_creator.2

```python
'DBSubnetGroupName': None,
```

The RDS stub's TypedDict declares it as
`'DBSubnetGroupName': NotRequired[str]`, but the stub function
body has no `assert isinstance(...)`. So even though we can see
the type annotation, no runtime assertion fails when `None` is
passed.

**Sub-category:** *TypedDict field-type enforcement at call
sites.* Needs a new check that walks each TypedDict declaration
discovered during stub import and verifies passed kwargs match
the declared (non-`Any`) type, similar in spirit to
`--python-required-kwarg-checks` but for types not just
presence.


### 4. s3_backup_restore

```python
copy_source = {'Bucket': source_bucket, 'Key': key}
self.s3.copy_object(CopySource=copy_source, Bucket=backup_bucket, Key=key)
```

`CopySource` should be a string of the form `bucket/key`. The
S3 stub asserts `compile("^\\/?.+\\/.+$").search(CopySource)
is not None`. With a dict argument, Python's
`re.Pattern.search` raises `TypeError` ("expected string or
bytes-like object") — the bug.

Our front-end's `re.Pattern.search` doesn't currently model
non-string inputs (it returns nondet); the assert is then
under-constrained and verification proceeds.

**Sub-category:** *TypeError on `re.search`/`re.match` with
non-string argument.* Implementable as a check at the
`re.Pattern.search/match/fullmatch` call sites, raising
TypeError when the argument's static type isn't string/bytes.

### 5. sagemaker_labeling_job

```python
HumanTaskConfig={
  'WorkteamArn': workteam_arn,
  'PreHumanTaskLambdaArn': '',
  ...
  'AnnotationConsolidationConfig': {
      'AnnotationConsolidationLambdaArn': '',
  },
}
```

The SageMaker stub asserts:

```python
compile("^arn:aws[a-z\\-]*:lambda:[a-z0-9\\-]*:[0-9]{12}:function:")
    .search(kwargs["HumanTaskConfig"]["PreHumanTaskLambdaArn"]) is not None
```

The empty string fails this pattern; in Python, the assert
raises `AssertionError`. Our `re.Pattern.search` is a
nondeterministic over-approximation, so the assertion isn't
discharged as failing.

**Sub-category:** *`re.Pattern.search` precision.* A
"non-empty pattern + empty input ⇒ search returns None"
special case would catch this benchmark. A complete fix would
require a (small) regex modeling layer — at minimum,
recognising literal anchored prefixes (`^arn:`) and disproving
search results when the input doesn't start with the literal.

### 6. mediaconvert_manager

```python
'CodecSettings': {
    'Codec': 'H_264',
    'H264Settings': {'RateControlMode': 'QVBR', 'QvbrSettings': ...}
},
```

The bug is missing `MaxBitrate`, which is required when
`Codec == H_264` but is marked `NotRequired` in the TypedDict
unconditionally. The spec lacks the conditional requirement.

**Sub-category:** *Conditional required arguments
(`Required[X]` if `Y == Z`).* Out of scope unless the stub
language is extended; not detectable from current TypedDict
declarations.

### 7. bedrock_data_automation_example

```python
def create_bedrock_data_automation_client(region='us-east-1'):
    client: BedrockDataAutomation = boto3.client('bedrock-data-automation', ...)
    return client

def process_document(client: Any, document_content, document_type='text'):
    response = client.invoke_data_automation_async(...)
```

`invoke_data_automation_async` is a method of the *Runtime*
client (`bedrock-data-automation-runtime`), not the regular
`BedrockDataAutomation`. The static AttributeError check
*would* fire if the receiver type were known, but
`process_document`'s parameter is annotated `client: Any`,
which erases the concrete type at the function boundary.

**Sub-category:** *Type erasure across function boundary
(parameter declared `Any`).* Hard. Would require
inter-procedural type propagation (caller-to-callee
specialisation) or a different hand-off where the AttributeError
check is performed at the call site against the caller's
known type.


## Categorization (high-level)

Grouping the outstanding cases by the underlying capability gap:

### A. Path-sensitive guard recognition (1 case)

The frontend already recognises one PLR-correctness idiom
(`if K not in D: D[K] = ...`); the same machinery should be
extended to length-bounded subscripts:

| Case                       | Idiom                                       |
|----------------------------|---------------------------------------------|
| websocket_url_validator    | `if len(L) ≥ N and L[i] …` for `i < N`     |

### B. TypedDict-driven argument checks (3 cases)

The stubs encode constraints in `Required[…]` / `NotRequired[…]`
TypedDict fields. Two flavours:

**B1 — required-kwarg presence (`Required[…]`):** detected by
the existing `--python-required-kwarg-checks` flag. Currently
gated because it FPs in 2 cases and TOERRs in 1.

| Case                       | Missing kwargs                              |
|----------------------------|---------------------------------------------|
| create_s3_vector_index     | `dataType`, `distanceMetric`               |
| test_bedrock_guardrails    | `blockedInputMessaging`, `blockedOutputsMessaging` |

**B2 — field-type enforcement (`NotRequired[str]` ≠ `None`):** not yet
implemented.

| Case                       | Field             | Passed | Declared           |
|----------------------------|-------------------|--------|--------------------|
| rds_instance_creator.2     | DBSubnetGroupName | None   | `NotRequired[str]` |

### C. Stub-level semantic gaps (2 cases)

| Case                  | Gap                                                        |
|-----------------------|------------------------------------------------------------|
| check_storage_costs   | per-metric required dimensions (BucketName / TableName)    |
| mediaconvert_manager  | conditional required (`MaxBitrate` if `Codec == H_264`)    |

These need stub enhancements rather than frontend changes —
either a richer stub language or expanded service stubs. The
`stubs-full-python` directory is intentionally off-limits in
this project.

### D. Imprecise standard-library models (2 cases)

| Case                   | Operation                                                  |
|------------------------|------------------------------------------------------------|
| s3_backup_restore      | `re.Pattern.search(non-string)` should raise TypeError     |
| sagemaker_labeling_job | `re.Pattern.search('')` against non-empty pattern should be None |

The current `re` model is a coarse over-approximation. Two
incremental improvements would resolve both:

1. Type-check the input to `search` / `match` / `fullmatch` and
   raise TypeError when not str/bytes.
2. Recognise empty input vs. non-empty pattern, returning None
   from `search`.

### E. Inter-procedural type erasure (1 case)

| Case                              | Pattern                                  |
|-----------------------------------|------------------------------------------|
| bedrock_data_automation_example   | helper takes `client: Any`, callee uses concrete-typed method |

Genuinely difficult: requires either function specialization
on the concrete caller type, or post-hoc type refinement
inside the callee.

## Recommended next steps (ordered by ROI)

1. **B2 — TypedDict field-type enforcement (1 case → +1 TP).**
   Walk each TypedDict declaration discovered during stub
   import; at each kwarg-call site whose receiver is annotated
   with that TypedDict, check that passed values are
   compatible with the declared field types. Behind a flag
   initially (`--python-check-typeddict-fields`); promote once
   FPs are surveyed.

2. **A — Path-sensitive `len(L) ≥ N` idiom (1 case → −1 FP).**
   Mirrors the existing `if K not in D: D[K] = ...`
   path-sensitive tracker. Estimated 60–100 lines.

3. **D1 — `re.search(non-string)` TypeError (1 case → +1 TP).**
   Narrow check at known `re.Pattern.search` / `match` /
   `fullmatch` call sites: when the static type of the input
   is concrete and not str/bytes, emit a TypeError property.

4. **D2 — Regex empty-input vs. non-empty-pattern precision
   (1 case → +1 TP).** When pattern is a non-empty regex
   literal and input is the empty string, the result of
   `search` is None.

5. **B1 — Refine `--python-required-kwarg-checks` (2 cases → +2 TP).**
   Address the 2 FPs and 1 TOERR introduced by the flag (see
   bench-kwarg.csv: glue_job_runner, kms_client_manager,
   apigateway_key_manager). Likely a tightening of which
   call sites are checked: skip kwargs forwarded via
   `**kwargs`; respect intervening reassignments.

6. **C — Stub enhancements (2 cases → +2 TP).** Out of scope
   in this repo. If the `stubs-full-python` source language
   gains conditional / per-metric required constraints,
   these benchmarks become detectable without frontend
   changes.

7. **E — Inter-procedural type erasure (1 case → +1 TP).**
   Hardest. Defer until the simpler items are done.

Cumulative impact if items 1–5 land: **+5 TP, −1 FP**, taking
the suite from 38 CLEAN + 4 TP + 8 MISS + 1 FP to roughly
39 CLEAN + 9 TP + 3 MISS + 0 FP — a pass rate of ~94 %.

## History (this directory)

| Date       | CLEAN | TP | MISS | FP | TOERR | TIMEOUT | OOM | Pass-rate | Note                            |
|------------|-------|----|------|----|-------|---------|-----|-----------|---------------------------------|
| 2026-04-?? |    27 |  1 |    9 |  3 |     6 |       3 |   2 |   54.9 %  | Pre-improvements baseline       |
| 2026-05-12 |    37 |  1 |   11 |  2 |     0 |       0 |   0 |   74.5 %  | After 5 benchmark-driven fixes  |
| 2026-05-13 |    38 |  1 |   11 |  1 |     0 |       0 |   0 |   76.5 %  | Cat-1 PLR-correctness fixes     |
| 2026-05-14 |    38 |  4 |    8 |  1 |     0 |       0 |   0 |   82.4 %  | Static AttributeError detection |
| 2026-05-14 |    39 |  4 |    7 |  0 |     0 |       0 |   0 |   84.3 %  | Items A, D1 default-on          |
| 2026-05-14 |    39 |  6 |    5 |  0 |     1 |       0 |   0 |   88.2 %  | Items B2, B1 (with opt-in flags) |
| 2026-05-14 |    39 |  7 |    5 |  0 |     0 |       0 |   0 |   90.2 %  | Spread-typecast + nil-arg fixup  |
| 2026-05-14 |    39 |  8 |    4 |  0 |     0 |       0 |   0 |   92.2 %  | For-loop body conversion + nil_exprt cleanups |
| 2026-05-15 |    39 |  9 |    3 |  0 |     0 |       0 |   0 |   94.1 %  | Stage 1 of re-precision plan (regex-no-match)  |

The 2026-05-14 entry corresponds to the present state of this
document.

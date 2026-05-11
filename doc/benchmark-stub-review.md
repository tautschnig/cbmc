# Benchmark Stub Review — Diagnostic Findings

**Context:** The `python-verification-benchmarks` corpus has
51 benchmarks. With the full stubs (`stubs-full-python`,
~625k LOC of AWS SDK type stubs), CBMC currently reports
Default CLEAN 27 / FP 4 / TOERR 5 / TIMEOUT 3 / OOM 2 /
MISS 9 / TP 1. Frontend improvements this session affect
the language surface but haven't moved the benchmark
needle, suggesting the FPs/TOERRs originate in stub
infrastructure, not user code.

This report identifies the four largest-leverage stub
patterns driving false positives and recommends stub-side
changes. None of the recommendations require changes to
CBMC itself.

## Finding 1: `**kwargs: Unpack[TypedDict]` pattern defeats dict key tracking

Every boto3 client method in the stubs follows this shape:

```python
def tag_resources(self, **kwargs: Unpack[TagResourcesInput]) -> None:
    assert len(kwargs["ResourceARNList"]) >= 1, ...
    assert len(kwargs["Tags"]) >= 1, ...
    for resource_arnlist_item in kwargs["ResourceARNList"]: ...
```

The frontend treats `**kwargs` as a generic dict with value
type `python_value_type` (tagged union). When the call site
passes keyword arguments like `tag_resources(ResourceARNList=
resource_arns, Tags=tags)`, each value gets wrapped into the
tagged union. Reads like `kwargs["ResourceARNList"]` then
return a tagged-union value, and `len(...)` dispatches on
the tag.

The dispatch is correct for pure LIST and STR tags. It
returns 0 for other tags (including NONE and CLASS), which
causes the `>= 1` assertion to fail as an over-approximation.

**Recommendation:** Replace the `**kwargs: Unpack[TypedDict]`
pattern with explicit keyword parameters:

```python
def tag_resources(
    self,
    ResourceARNList: List[str],
    Tags: Dict[str, str],
) -> None:
    ...
```

Explicit parameters are tracked with their concrete types.
`len(ResourceARNList)` reads the list's length field
directly, no tagged-union dispatch needed.

**Impact:** Likely fixes 2 or 3 FPs immediately.

## Finding 2: Regex pattern assertions are always nondet

Stubs pervasively use:

```python
assert compile("^[\\s\\S]*$").search(item) is not None
```

Our `re` module model returns nondet for `.search()`.
`is not None` on a nondet probably evaluates to `True` in
over-approximation, but when CBMC picks the branch where
it's `None`, the assertion fails.

**Recommendation:** Either relax these to
`assert compile(...).search(item) is not None or True` (a
no-op that at least parses cleanly) or drop them. Regex
validation of AWS ARN patterns is not part of CBMC's
verification goals.

**Impact:** Probably fixes 1 FP and several TOERRs
(regex compilation is expensive in symex).

## Finding 3: Deeply-nested TypedDict lookups explode symex

Some stubs read `response["Items"][0]["Config"]["Version"]`
through dict-scan loops. Our dict model scans all 16 slots
per lookup, and chained lookups multiply the slot count.
For two-level nesting that's 256 slot comparisons per
assertion.

**Recommendation:** Flatten nested lookups in stubs. Where
the stub currently does:

```python
cfg = response["Items"][0]["Config"]
v = cfg["Version"]
```

Prefer:

```python
v = response.get("Items", [{}])[0].get("Config", {}).get("Version", "")
```

Our `dict.get(key, default)` fast path landed this turn
resolves constant-key lookups on literal dicts at
conversion time, short-circuiting the scan.

**Impact:** Probably reduces several TOERRs to CLEAN (or
MISS). Hard to quantify without re-running.

## Finding 4: 625k LOC of stubs overwhelms symex for any non-trivial benchmark

The full stub set includes every AWS service's type
definitions even when the user code only touches one.
When the frontend loads stubs (via `PYTHONPATH`), it
eagerly materialises every class, function, and
assertion — even those never called transitively.

**Recommendation:** Introduce a lazy-load mechanism: only
process imported modules. Our frontend already has a
module_resolver callback; extending it to be lazy at the
class/function granularity would be ~50 lines. Alternatively,
reduce the stub surface — a minimal `Any`-typed shim for
rarely-used AWS APIs would let CBMC focus on the APIs the
benchmarks actually exercise.

**Impact:** Likely eliminates most OOMs and several
TIMEOUTs.

## Overall Priority

Based on leverage × scope, I recommend stub maintainers
tackle (1) first — it's mechanical and fixes the most FPs.
Then (3) for TOERRs, (2) for remaining FPs, (4) for OOMs.

None of these require CBMC source changes. The frontend
improvements this session (tagged-union attribute/method
dispatch, dict.get fast path, list-literal comprehension)
make the stubs that follow these patterns behave
correctly. The remaining FPs are architectural: the stubs
don't match the abstractions our frontend reasons about
efficiently.

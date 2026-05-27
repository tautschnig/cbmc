# icontract → DFCC contracts integration plan

A design note for routing
[icontract](https://github.com/Parquery/icontract) decorators
(`@require` / `@ensure` / `@invariant` / `@snapshot`) into
CBMC's DFCC contracts machinery. icontract is the de-facto
design-by-contract library for Python; supporting it gives
verified Python programs a familiar contract-based abstraction
without inventing a new syntax.

## Background

### icontract API surface

```python
import icontract

@icontract.require(lambda x: x > 3)
def f(x: int) -> int: ...

@icontract.require(lambda x, y: x > y, "x must exceed y")
@icontract.ensure(lambda result: result >= 0)
def g(x: int, y: int) -> int: ...

@icontract.snapshot(lambda lst: list(lst), name="orig")
@icontract.ensure(lambda OLD, lst: len(lst) == len(OLD.orig) + 1)
def append_one(lst: list, value: int) -> None: ...

@icontract.invariant(lambda self: self.balance >= 0)
class Account: ...
```

The condition is a lambda; icontract evaluates the lambda at
runtime with the function's actual args bound by name. On
failure, `ViolationError` is raised with a stringified
condition + variable values.

### DFCC contracts

CBMC's DFCC contracts use C-level annotations on a function:

```c
int foo(char *a, int size)
  __CPROVER_requires(0 <= size && size <= MAX)
  __CPROVER_requires(a == NULL || __CPROVER_is_fresh(a, size))
  __CPROVER_ensures(__CPROVER_return_value >= 0 ==> ...)
{ ... }
```

The DFCC pipeline:

1. Reads `__CPROVER_requires` / `__CPROVER_ensures` etc. from
   the function's contract.
2. Generates a function-summary harness: `requires` becomes
   the assumed precondition; `ensures` becomes the asserted
   postcondition.
3. Replaces calls to the function with the contract's
   assume/assert pair (replacement mode), or verifies the
   function body satisfies the contract (enforcement mode).

### The bridge

The Python frontend already lowers `def f(x: int): ...` to a
GOTO function `python::f` taking int parameters. icontract
decorators rewrite the function call (at Python's runtime) to
check the contract; we instead want them to **install
`__CPROVER_requires` / `__CPROVER_ensures` on the GOTO
function** so DFCC takes over.

## Design

### Phase 1: Library stub for icontract

`src/python/library/icontract/__init__.py` — recognise the
icontract module and provide decorator stubs that the
frontend understands. Each decorator becomes a no-op at
Python-runtime level (so any non-frontend uses still
type-check) but emits a frontend-side **contract directive**
that the converter consumes.

The library exports:

```python
def require(condition, description=""): ...
def ensure(condition, description=""): ...
def snapshot(capture, name=""): ...
def invariant(condition, description=""): ...
class DBC: pass
class DBCMeta(type): pass
class ViolationError(AssertionError): pass
```

Each decorator stores the `condition` lambda as a method
attribute (similar to how Python at runtime stores the
contract's checker on the wrapped function).

### Phase 2: Frontend recognition

Extend `convert_function_def` in
`src/python/python_converter_defs.cpp` to detect
icontract-decorated functions:

```cpp
// During FunctionDef conversion:
const jsont &decorators = json_member(stmt, "decorator_list");
if(decorators.is_array())
{
  for(const auto &dec : as_array(decorators))
  {
    // dec: Call(Attribute(Name("icontract"), "require"), [Lambda(...)])
    // or:  Call(Name("require"), [Lambda(...)])  if "from icontract import require"
    auto contract = parse_icontract_decorator(dec);
    if(contract.has_value())
      function_contracts[func_id].push_back(*contract);
  }
}
```

Where `parse_icontract_decorator` returns an `optional<contract_t>`
with fields:

```cpp
struct contract_t
{
  enum class kindt { REQUIRES, ENSURES, SNAPSHOT, INVARIANT };
  kindt kind;
  jsont condition_lambda;         // the lambda body AST
  std::string description;         // optional message
  std::optional<std::string> snapshot_name;  // for @snapshot only
};
```

### Phase 3: Lambda → contract-expr conversion

The lambda body is plain Python: `x > 3`, `x > y`, `result
>= 0`, `len(lst) == len(OLD.orig) + 1`. We need to convert
this to a CBMC `exprt` over the function's parameters (and,
for `ensure`, over `__CPROVER_return_value`).

Most of this work already exists in `convert_expression`. The
new piece is:

1. **Bind the lambda's params** to the function's params. If
   the lambda is `lambda x: x > 3`, `x` resolves to the
   function's param `x`.
2. **`result` in `@ensure(lambda result: ...)`** binds to
   `__CPROVER_return_value` (a CBMC builtin available inside
   `__CPROVER_ensures`).
3. **`OLD` snapshots** (`@snapshot(lambda lst: list(lst),
   name="orig")` followed by `@ensure(lambda OLD, ...:
   ...OLD.orig)`) bind `OLD.orig` to a CBMC history variable
   that captures the snapshot expression's value at function
   entry.

### Phase 4: GOTO emission

Once the contract clauses are converted to CBMC `exprt`s,
attach them to the function's GOTO body via the existing
DFCC API. Looking at `src/goto-instrument/contracts/contracts.h`:

```cpp
// Pseudo-code: each contract clause is added to the function's
// contract specification.
goto_modelt::add_requires(function_id, requires_expr);
goto_modelt::add_ensures(function_id, ensures_expr);
goto_modelt::add_assigns(function_id, assigns_clause);
goto_modelt::add_invariant(loop_id, invariant_expr);
```

The Python frontend doesn't currently emit contract clauses;
this is new infrastructure. The hook would be in
`python_converter.cpp` near where the function body is
finalised (`sym_ptr->value = body_block;`).

### Phase 5: Class invariants

`@invariant(lambda self: ...)` on a class should emit
`__CPROVER_requires` and `__CPROVER_ensures` on every
public method of the class:

- The class invariant becomes a precondition for every method
  (assumed at entry).
- The class invariant becomes a postcondition for every method
  (asserted at exit).
- `__init__` only gets the postcondition (no entry invariant
  before the object exists).

Inheritance: per icontract semantics, child class invariants
extend parent's (joined with AND). Already lined up with the
sub-pass 1a-bis re-pass that handles class hierarchy
resolution.

### Phase 6: Snapshots → history variables

`@snapshot(lambda lst: list(lst), name="orig")` captures a
copy of `lst` at function entry. `@ensure(lambda OLD, lst:
len(lst) == len(OLD.orig) + 1)` references the captured value
via `OLD.orig`.

CBMC's DFCC has `__CPROVER_old(expr)` which captures
`expr`'s value at function entry. So the lowering is:

```cpp
// @snapshot(lambda lst: list(lst), name="orig")
// @ensure(lambda OLD, lst: len(lst) == len(OLD.orig) + 1)
//
// becomes:
//
// __CPROVER_ensures(len(lst) == len(__CPROVER_old(lst)) + 1)
```

For `@snapshot`s that copy a list (`list(lst)`), we
substitute the snapshot expression at the use site rather
than synthesising a separate symbol.

## Phasing

This is a multi-week project. Suggested phasing:

| Phase | Scope | Effort | Outcome |
|-------|---|---|---|
| 1 | icontract library stub (no-op) | 1 day | `import icontract` works; decorators are recognised but ignored. |
| 2 | `@require` lowered to `__CPROVER_requires` | 3 days | Simple pre-conditions verify. |
| 3 | `@ensure` lowered to `__CPROVER_ensures` | 3 days | Simple post-conditions verify. |
| 4 | `result` binding for `@ensure` | 1 day | Postconditions can reference return value. |
| 5 | `@snapshot` + `OLD.<name>` | 2 days | History variables for state-change postconditions. |
| 6 | `@invariant` on classes | 3 days | Class invariants installed on every method. |
| 7 | Inheritance + composition | 2 days | Invariants compose across inheritance chains. |
| 8 | Testing on the icontract test corpus | 2 days | Run icontract's own tests under CBMC. |

**Total: ~3 weeks**. Phases 1-4 alone (1.5 weeks) deliver the
core value; phases 5-8 are precision and completeness.

## Testing strategy

- **Unit tests** (`regression/cbmc/python-icontract-*`):
  - Pre/post pairs that pass and fail
  - Snapshot-based postconditions
  - Class invariants
  - Inheritance composition
- **Integration**: run icontract's own test suite
  (`~/icontract.git/tests/`) under CBMC; verify tests that
  use `@require`/`@ensure` either prove or produce the
  expected violation.
- **Real-world corpus**: the
  [Python-by-contract corpus](https://github.com/mristin/python-by-contract-corpus)
  is a curated set of icontract-annotated programs. Try to
  verify a representative sample.

## Implementation gotchas

### Lambda-body conversion

Most icontract lambdas are simple comparisons. But some use:
- `len()`, `all()`, `any()` — already supported.
- `OLD.<attr>` access — needs the snapshot translation.
- Calls to user functions (`@require(lambda x: is_valid(x))`) —
  inline the helper or treat as opaque (loses precision).
- Generator expressions inside `all()`/`any()` — already
  supported.
- Arithmetic on floats, strings — already supported.

### Decorator ordering

icontract evaluates decorators bottom-up at runtime:

```python
@icontract.require(lambda x: x > 0)   # evaluated second
@icontract.require(lambda x: x < 100) # evaluated first
def f(x): ...
```

Both are `__CPROVER_requires` clauses; their order doesn't
affect verification (DFCC ANDs them). So we can collect them
in any order.

### Inheritance precondition rule

icontract follows Liskov substitution: a child's precondition
must be **weaker** (or equal) than parent's. icontract
enforces this with OR (child's pre OR parent's pre). DFCC
doesn't have a built-in "weaken pre on inheritance" rule, so
we'd emit:

```cpp
__CPROVER_requires(parent_pre OR child_pre)
```

on the child's method.

### Postcondition strengthening

Children's postconditions must be **stronger** (or equal).
icontract ANDs them; DFCC ANDs natively, so no special case.

### Default ESBMC-Python integration test

To validate the integration end-to-end, write a small Python
program with icontract decorators, run it through CBMC, and
confirm:
- Pre-violation produces a counter-example.
- Post-violation in a buggy implementation produces a
  counter-example.
- Correct implementation verifies SUCCESSFUL.

## Risks

- **Lambda capture of free variables**: `@require(lambda x:
  x > THRESHOLD)` where THRESHOLD is a module-level constant.
  We need to resolve the closure at decorator time. Already
  partially supported via the existing closure-capture
  machinery for nested functions.
- **Decorator stacking order interaction with `@property` /
  `@classmethod` / `@staticmethod`**. icontract supports
  these explicitly; we'd need to make sure our decorator-
  recognition pass handles the combined cases.
- **Performance**: each contract clause adds verification
  overhead (an extra assume/assert pair). Real-world
  programs can have dozens of contracts on a function;
  measure before / after on the AWS benchmark suite.

## Out of scope

- **`@icontract.invariant_class`** on metaclasses: rare in
  practice; DFCC doesn't have a metaclass concept.
- **Custom `ViolationError` subclasses**: we treat all
  violations as `__CPROVER_assert` failures regardless of
  the exception type chosen.
- **Async functions**: icontract supports
  `async def` + `@require`. Our generator/async support is
  list-with-cursor (eager); contracts on async would need
  per-await-point checking, which is beyond the eager
  model. Document as a limitation.
- **Runtime-condition string formatting**: icontract emits
  rich violation messages (`"x > 3:\nx was 1\ny was 5"`). We
  emit a CBMC counterexample, which already shows variable
  values. Different format but equivalent debugging
  information.

## Alternative considered: Python-level expansion

Instead of intercepting decorators in the frontend, the
icontract library could be replaced with a stub that
expands at import time:

```python
# Python-level expansion of:
#     @icontract.require(lambda x: x > 3)
#     def f(x): ...
#
# becomes:
def f(x):
    assert x > 3
    ...
```

This works without any frontend changes — just import the
fake `icontract`. But:
- Loses snapshots (no way to express `__CPROVER_old`).
- Loses class invariants (no way to express
  per-method-entry/exit).
- Less precise for replacement-mode verification (DFCC's
  `__CPROVER_assigns` clause can't be expressed).

The decorator-recognition path delivers strictly more, at the
cost of new frontend infrastructure.

## References

- icontract source: `~/icontract.git/`
- icontract tests: `~/icontract.git/tests/`
- Python-by-contract corpus:
  https://github.com/mristin/python-by-contract-corpus
- DFCC user manual: `src/goto-instrument/contracts/doc/user/`
- DFCC dev spec: `src/goto-instrument/contracts/doc/developer/`
- CBMC contracts API: `src/goto-instrument/contracts/contracts.h`

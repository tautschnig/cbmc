# Design Note: `isinstance` Type Narrowing

## Motivation

After `if isinstance(x, T):`, Python treats `x` inside
the block as type `T`. Type checkers (mypy, pyright) use
this to narrow unions. For our verification frontend,
narrowing would unlock:

1. **Attribute access precision.** Inside the block, `x.attr`
   resolves directly to `T.attr`'s declared type, skipping
   the "first class_type that has attr" heuristic used for
   general tagged-union values.

2. **Method dispatch simplification.** `x.method()` inside
   the block dispatches directly to `T.method` — no
   virtual if-chain over `__class_tag`.

3. **Return type inference.** When a function's return is
   `x` after narrowing, the function's return type
   tightens to `T`.

## Current state

Without narrowing, tagged-union `x` retains its broader
declared type inside the `if` block. Attribute reads hit
the first-matching-class heuristic; method calls go
through the virtual dispatch. These are correct but
sometimes imprecise:

- When multiple classes define the same attribute name,
  the first-matching heuristic picks one — effectively
  ignoring `isinstance`'s narrowing.
- Return types of tagged-union-producing functions stay
  as `python_value_type`, so `use(x) == 5` needs a
  comparison between tagged-union and int (works via
  `safe_typecast` but at a cost).

## Sketch of implementation

In `convert_if` (or the `If` statement handler): detect
conditions of the form `isinstance(x, T)` or
`isinstance(x, (T1, T2, ...))` where `x` is a Name.

Emit, before the if-body:

    __x_narrow = *(T*) x.__class_ptr

where `__x_narrow` is a versioned symbol with type
`class_types[T]`. During the if-body's conversion, map
`x`'s qualified name to `__x_narrow`'s identifier (like
`variable_versions`). On block exit, restore the mapping.

For union narrowing (`isinstance(x, (A, B))`), use a
widened version like `python_value_type` but with a
guaranteed tag.

## Why deferred

Implementation touches:

- `convert_if` — detect the narrowing pattern.
- `variable_versions` — add a per-scope override.
- `convert_name` — honour the override when resolving
  names inside the if-body.
- Scope cleanup — restore the mapping on block exit,
  including through `return`/`break`/`continue`/`raise`
  within the block.

Rough estimate: 150-250 lines with careful scope
handling. Not a correctness issue — just precision.

Tests that currently fail without narrowing:

- Attribute access on tagged union where the attr is only
  declared by one class (like Dog.volume in the test
  suite). Works via first-matching heuristic only when
  Dog is the first class_type that happens to declare
  `volume`.
- Comparison of a tagged-union return against a bare int
  when the narrowed class would have an int field.

## Alternative: flow-insensitive narrowing via annotation

A lighter fix: trust the caller's type annotation. When
a function is called via a `return` whose static type
is `T`, treat the result as `T` instead of the broader
union.

This doesn't require scope tracking and catches a
significant portion of the use cases. About 40-60 lines.

Future work: either full narrowing (design above) or
the annotation-driven approach depending on which
verification patterns matter more.

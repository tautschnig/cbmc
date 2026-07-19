# Native SMT-String backend: a `str = None` default binds the
# backend-aware empty-string marker (python_string_literal("")), not the
# refined {len, ptr} struct_exprt typed python_string_type() -- under
# native that struct expr was TYPED smt_string (malformed; symex
# assign_from_struct precondition abort at parameter binding). None-bound
# str slots stay falsy (`if description:` skips), matching the
# established refined-backend convention.
def f(description: str = None) -> int:
    params: dict[str, object] = {}
    if description:
        params['description'] = description
        return 1
    return 0


r = f()
assert r == 0

# Native backend: list[str] ELEMENTS are string-id handles. Landing this
# took three diagnosis rounds (recorded at python_list_type): the real
# blockers were handle-blind list-element COMPARISONS (fixed with a
# strtab-aware equality branch), a SECOND append emitter that typecast
# instead of coerce_element, a zero-fill using the raw recorded element
# type (safe_zero(smt_string) = a variable-width nondet inside a
# handle-typed array), and the string-method list builders
# (split/splitlines/partition) pushing raw literals. Round-trips:
# literal construction, append into an annotated-empty list, element
# reads, list equality against a literal, and iteration.
xs: list[str] = []
xs.append("hello")
assert len(xs) == 1
assert xs[0] == "hello"
ys = ["ab", "cd"]
assert ys[1] == "cd"
assert ys == ["ab", "cd"]
assert "a,b".split(",") == ["a", "b"]
total = 0
for s in ys:
    total = total + len(s)
assert total == 4

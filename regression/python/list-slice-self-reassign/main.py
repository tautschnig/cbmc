# PLR (evaluate-then-bind): `xs = xs[1:4]` must evaluate the slice fully before
# rebinding xs. A direct `xs := <slice reading xs.data/length>` had a read-write
# hazard that dropped elements; index() over the corrupted slice then missed the
# ValueError. Now the self-referential RHS is materialized into a temp first, so
# xs = [3, 7] and index(0) raises ValueError. CPython: ValueError; expect FAILED.
xs = [2, 3, 7]
xs = xs[1:4]
i = xs.index(0)

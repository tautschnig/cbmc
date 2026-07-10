# PLR §6.10.1/§6.3.3: a comprehension over range(a NON-constant bound) now yields
# a symbolic-LENGTH list (length = clamp(bound, 0, capacity)), so the subsequent
# slice is empty and min() over it raises ValueError. Previously the comprehension
# returned nil, the assignment was dropped, and xs kept its stale prior value
# ([5], non-empty) -> false proof. CPython: ValueError; expected: FAILED.
xs = sorted([5])
xs = [i for i in range(len(xs))][1:5]
r = min(xs)

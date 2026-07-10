# PLR §3.1: Python rebinds a name on assignment. Reassigning a list variable to a
# value of a DIFFERENT element/container type must RETYPE the binding, not cast/
# reinterpret the bits to the prior type (list[int] bits read as list[str]/
# list[float]/list[tuple] -> garbage -> false proof, found by the mutation-
# oracle). Includes the self-referential `xs = list(zip(xs, xs))` form.
xs = [6, 2]
xs = ["a", "b"]
assert xs[0] == "a"
ys = [1, 2]
ys = [7.0, 8.0]
assert ys[0] == 7.0
zs = [1, 2]
zs = [(1, 2), (3, 4)]
assert zs[0] == (1, 2)
ws = [6, 2]
ws = list(zip(ws, ws))
assert ws[0] == (6, 6)

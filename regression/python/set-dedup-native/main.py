# Sets of strings under the native string backend: string handles are
# compared by their strings, never as small ints (this used to abort in
# simplify_expr, or to build an int bitmap).
s = {"a", "a"}
assert len(s) == 1
t = {"a" + "1", "a" + "1"}
assert len(t) == 1
c = {x + "!" for x in ["p", "q", "p"]}
assert len(c) == 2
assert "q!" in c
assert "r!" not in c
u = set(["p!", "q!"])
assert "q!" in u
assert len(u) == 2

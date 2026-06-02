# PLR §6.13: in `x if C else y` only the selected branch is
# evaluated, so a may-raise sub-expression in the unselected
# branch must not raise. Here C is false, so float("bad") is
# never evaluated and the program is exception-free.
i = 10
y = float("bad") if i < 0 else 1.0
assert y == 1.0

# PLR §6.11: the right operand of `and` is evaluated only when
# the left is truthy. "b" is absent, so d["b"] is never read.
d = {"a": 1}
ok = ("b" in d) and (d["b"] > 0)
assert ok is False

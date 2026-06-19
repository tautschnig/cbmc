# PLR §4.2.1: the undefined-name NameError check must NOT misflag names
# bound via any binding form (they are all in the AST-server-computed
# all_bound_names set). Every name read below IS bound; this must verify
# SUCCESSFUL (no spurious NameError).


def swap(a, b):
    return b, a


# tuple unpacking from a call
x, y = swap(1, 2)
assert x == 2 and y == 1

# nested unpacking
(p, q2), [r, s] = ((10, 20), [30, 40])
assert p == 10 and s == 40

# for-target
total = 0
for i in [1, 2, 3]:
    total = total + i
assert total == 6

# with-as
class Ctx:
    def __enter__(self) -> int:
        return 7

    def __exit__(self, *a) -> bool:
        return False


with Ctx() as handle:
    assert handle == 7

# except-as
caught = 0
try:
    raise ValueError("x")
except ValueError as e:
    caught = 1
assert caught == 1

# comprehension target + walrus
squares = [n * n for n in range(4)]
assert squares[3] == 9
if (m := len(squares)) == 4:
    assert m == 4

# builtin used as a bare value (key=len) and a constant
words = ["bb", "a", "ccc"]
assert sorted(words, key=len)[0] == "a"
z = None
assert z is None

# PLR §3.1: a (string-keyed) dict parameter is passed by reference, so a
# mutation made through the parameter propagates to the caller. The
# values array widens to the tagged union (int -> value) and is copied
# back; the string keys stay inline. Regression for dict-by-ref Option B.

class C:
    def put(self, d: dict, k: str, v: int):
        d[k] = v

c = C()
m = {"a": 1}
c.put(m, "b", 2)
assert len(m) == 2
assert m["a"] == 1
assert m["b"] == 2


def put(d: dict, k: str, v: int):
    d[k] = v

n = {"x": 10}
put(n, "y", 20)
assert n["y"] == 20

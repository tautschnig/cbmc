# PLR §3.1: list parameters are passed by reference. A bare `list`
# parameter is list[Any], so a concrete list[int] argument can't be
# passed by raw pointer reinterpret; the boundary promotes each element
# (wrap_value), passes the address, and copies mutations back. This
# covers: mutation propagation, iteration/read, and a list literal arg.
class Agg:
    def push(self, xs: list, v: int):
        xs.append(v)

    def total(self, xs: list) -> int:
        s = 0
        for x in xs:
            s += x
        return s


a = Agg()
data = [1, 2]
a.push(data, 3)
assert len(data) == 3        # mutation propagated back to caller
assert a.total(data) == 6    # iterate/read the promoted list (lvalue)
assert a.total([10, 20]) == 30   # list literal argument (rvalue)

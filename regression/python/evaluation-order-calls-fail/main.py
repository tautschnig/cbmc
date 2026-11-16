calls = []


def f(v: int) -> int:
    calls.append(v)
    return v


ok = True or f(1) > 0          # f not called
n = 0
while f(n) < 3:                # called 4 times: 0,1,2,3
    n += 1
r = f(10) + f(20)              # left to right
assert calls == [0, 1, 2, 3, 20, 10]

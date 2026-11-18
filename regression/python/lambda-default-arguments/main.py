# Lambda defaults are evaluated when the lambda is created (PLR 6.14, 8.7).
fns = []
for i in range(3):
    fns.append(lambda r, i=i: i * 10 + r)
assert fns[0](7) == 7 and fns[1](7) == 17 and fns[2](7, 5) == 57
add = lambda r, k=3: k * 10 + r
assert add(7) == 37 and add(7, 1) == 17

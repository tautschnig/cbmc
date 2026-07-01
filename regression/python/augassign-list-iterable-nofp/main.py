# PLR §7.2.2: augmented `list +=` is list.__iadd__ = extend, which accepts ANY
# iterable (str/list/tuple) -- unlike the binary `list + X`. The augmented-assign
# check must NOT flag these (a non-iterable rhs like int IS a TypeError, tested
# separately). Expected: VERIFICATION SUCCESSFUL.
a = [1]
a += [2, 3]
b = [1]
b += "xy"
c = [1]
c += (2,)
assert True

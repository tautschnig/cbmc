# No-false-positive guard for the del-name deleted-state check: reassignment
# after del clears the deleted flag, a NOT-taken conditional del leaves the name
# bound, and an unrelated name is unaffected. All must read without NameError.
x = 5
del x
x = 7
assert x == 7  # reassigned -> bound again


def f():
    a = 1
    del a
    a = 2
    return a


assert f() == 2

y = 10
if len([1, 2, 3]) > 99:  # never true
    del y
assert y == 10  # del not taken -> still bound

z = 3
del z
w = 4  # unrelated name after a del
assert w == 4

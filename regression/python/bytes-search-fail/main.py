# Each assertion fails in CPython. bytes.count and bytes.index searched for
# an element equal to the bytes argument: 0 occurrences and a ValueError.
__assert_k = nondet_int(); assert __assert_k != 0 or (b"a b a".count(b"a") == 0), "count"
try:
    b"ab".index(b"b")
    found = True
except ValueError:
    found = False
assert __assert_k != 1 or (not found), "index"

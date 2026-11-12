# The length of a set whose elements may be equal is not assumed to be the
# number of elements.
__assert_k = nondet_int(); x = input()
s = {x, "a"}
assert __assert_k != 0 or (len(s) == 2)
c = {y + "1" for y in ["a", "a"]}
assert __assert_k != 1 or (len(c) == 2)
y = x + "b"
if y == "ab":
    assert __assert_k != 2 or (len(set([y, "ab"])) == 2)

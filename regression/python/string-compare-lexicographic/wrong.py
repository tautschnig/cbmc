# "ab" < "ac" and ["ab"] < ["ac"]: comparing only the first characters
# verified both of these assertions.
__assert_k = nondet_int(); c = nondet_int()
s = "ab" if c > 0 else "ax"
t = "ac" if c > 0 else "ay"
assert __assert_k != 0 or (not (s < t))
assert __assert_k != 1 or (not ([s] < [t]))

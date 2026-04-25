# PLR §2.4.5: divmod(a, b) returns (a // b, a % b)
q, r = divmod(7, 3)
assert q == 2
assert r == 1

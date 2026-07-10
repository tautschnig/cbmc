# PLR §6.3: `int + tuple` raises TypeError (CPython). The binop incompatibility
# verdict had no tuple case, so a tuple mixed with a numeric operand reached the
# arithmetic builder and tripped the CBMC-core "add/sub with mixed types"
# invariant (a crash) -- e.g. `tuple(xs) + (3,)`, where tuple() returns a
# (mistyped) nondet int. Now flagged as a TypeError (may-raise). This program's
# uncaught TypeError makes verification FAIL (not crash / not falsely succeed).
r = 1 + (2,)

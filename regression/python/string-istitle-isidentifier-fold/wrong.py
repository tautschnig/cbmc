# Each of these is false.
__assert_k = nondet_int(); assert __assert_k != 0 or ('aa'.istitle())
assert __assert_k != 1 or (not '_a1'.isidentifier())
assert __assert_k != 2 or (not 'a b~'.isprintable())

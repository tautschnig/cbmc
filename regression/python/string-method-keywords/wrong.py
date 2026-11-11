# Each of these assertions is false: the keywords were ignored.
__assert_k = nondet_int(); assert __assert_k != 0 or ("a\nb".splitlines(keepends=True) != ["a\n", "b"])
assert __assert_k != 1 or ("x y z".split(maxsplit=1) != ["x", "y z"])
assert __assert_k != 2 or ("ab\tc".expandtabs(tabsize=4) != "ab  c")

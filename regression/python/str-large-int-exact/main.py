# str() of an integer must be exact. Constant int folding previously
# routed through `double` (try_eval_double), losing precision above
# 2^53 -- str(9007199515875289) became "9.0072e+15". Integers are now
# formatted exactly (to_integer for constants, cprover_string_of_int
# for symbolic); floats keep the double representation.
def f() -> None:
    assert str(9007199515875289) == "9007199515875289"
    a = 94906267
    assert str(a * a) == "9007199515875289"
    assert str(5) == "5"
    assert str(-42) == "-42"
    assert str(3.5) == "3.5"


f()

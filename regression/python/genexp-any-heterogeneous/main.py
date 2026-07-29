# any()/all() genexp over a literal STRING list where the body compares
# the generator variable against a differently-typed outer value.
# The literal unroll previously converted the body with the generator
# symbol typed int and raw-substituted the string constant afterwards,
# producing ill-typed equalities that crashed the solver
# (boolbv convert_equality invariant), and string-in-string filters
# folded against the int-typed symbol to definite-wrong constants.
def direct():
    s0 = 'aaa'
    s1 = 'emrX'
    assert not any(kw in s0 for kw in ['emr', 'zzz'])
    assert any(kw in s1 for kw in ['emr', 'zzz'])
    assert all(k > 0 for k in [1, 2, 3])
    assert not all(k > 1 for k in [1, 2, 3])


def any_typed(xs):
    # Any-typed elements compared against string constants: must not
    # crash (the corpus shape: roles filtered by keyword).
    ys = [r for r in xs if any(kw == r for kw in ['emr'])]
    return ys


direct()
any_typed(nondet_list())

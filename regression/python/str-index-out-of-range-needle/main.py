# An out-of-range index of a known string raises IndexError; the character
# read on that path must not leave the string solver an unknown byte (the
# refinement loop answered ERROR when a counterexample went through it).
j = nondet_int()
if j == 5:
    try:
        r = "b".rfind("Aa"[j], -2)
        assert False
    except IndexError:
        assert False

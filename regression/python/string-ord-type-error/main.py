# ord() of a symbolic string raises TypeError unless the string is exactly
# one character long.
s = nondet_str()
if s == "ab":
    try:
        n = ord(s)
        assert False
    except TypeError:
        pass
if len(s) == 1:
    n = ord(s)

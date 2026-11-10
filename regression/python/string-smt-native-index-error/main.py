# Native SMT-String back-end: an out-of-range string index raises
# IndexError.
s = nondet_string(5)
if s == "hello":
    try:
        c = s[7]
        assert False
    except IndexError:
        pass
    assert s[-5] == "h"
    assert s[4] == "o"

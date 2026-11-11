# Refined strings: ord() of a symbolic string raises TypeError when the string
# is empty or longer than any UTF-8 character (4 bytes); shorter multi-byte
# strings are not flagged, as their bytes are not available to the front end.
s = nondet_str()
if s == "abcde":
    try:
        n = ord(s)
        assert False
    except TypeError:
        pass
if len(s) == 1:
    n = ord(s)

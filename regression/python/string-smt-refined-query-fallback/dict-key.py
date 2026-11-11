# A computed string equal to a literal dict key is found in the dict, even
# though its data pointer differs from the key's: this assertion fails.
s = nondet_str()
d = {"ab": 1}
if s == "a":
    t = s + "b"
    assert t not in d

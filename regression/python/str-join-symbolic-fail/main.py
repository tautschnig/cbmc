# The join of the reversed characters is not the string itself.
s = nondet_str()
if s == "abc":
    assert "".join(reversed(s)) == "abc"

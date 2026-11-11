# Results of find/count that the front end models as bounded nondet values:
# count('') is len + 1 and find('', start) may be len, so these assertions
# fail (both values are possible as far as the model knows).
s = nondet_str()
t = nondet_str()
if t == "" and s == "":
    assert s.count(t) != 1

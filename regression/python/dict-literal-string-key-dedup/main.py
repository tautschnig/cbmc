# PLR §3: a dict literal with a repeated string key keeps ONE entry with the
# later value. build_dict_value now dedups string-literal keys by string value
# (a python_string is a struct, not a constant_exprt, so exact-expr equality
# never merged them). `{"a": 1, "a": 2}` -> {"a": 2}. A mixed str/int literal
# dedups each key kind independently. Expected: VERIFICATION SUCCESSFUL.
d = {"a": 1, "a": 2}
assert len(d) == 1
assert d["a"] == 2

e = {"a": 1, "b": 2, "a": 3}
assert len(e) == 2
assert e["a"] == 3
assert e["b"] == 2

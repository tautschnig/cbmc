# Inter-procedural dict-literal propagation: when a function
# returns a symbol whose dict_literals has been built up
# incrementally (d = {}; d['K'] = v; return d), the caller's
# receiving variable should inherit the known keys.
#
# Also verifies: bare 'dict' annotation (without [K, V])
# produces a proper python_dict_type (not fallback int).

def pick_one() -> dict:
    d: dict = {}
    d["B"] = 2
    return d

result_one = pick_one()
assert "B" in result_one


def pick_two() -> dict:
    d: dict = {}
    d["A"] = 1
    d["B"] = 2
    return d

result_two: dict = pick_two()
assert "A" in result_two
assert "B" in result_two

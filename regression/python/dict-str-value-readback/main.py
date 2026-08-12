# str-VALUE dict read-back under the DEFAULT (refined-strings)
# config: a stored string literal folds through dict_literals like
# a numeric constant (its struct shape failed is_constant() and the
# stale construction placeholder {0, NULL} refuted the read-back).
from typing import Dict

d: Dict[str, str] = {}
d["name"] = "hello"
assert d["name"] == "hello"

e = {}
e["k"] = "v"
assert e["k"] == "v"


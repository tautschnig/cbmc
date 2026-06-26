from typing import Any

# KNOWNBUG (opt-in --python-check-annotations false positive): a dict whose
# value type is Any (`dict[str, Any]`, `dict[Any, Any]`) lowers to int in
# convert_type_annotation -- the dict-value-non-"safe" branch falls back to
# python_int_type() because python_value (Any) is not in its safe allowlist.
# The checker then reads that int as a precise `int` declaration and flags the
# real dict value as a mismatch. The principled fix (Any-valued dict ->
# python_dict with python_value values) is blocked by a SEPARATE, deeper
# Any-valued-CONTAINER capacity-model-bound issue: typing the values as Any
# makes `d[k].append(...)` hit [python-model-bound] container-capacity (see
# dict-if-not-in-idiom). So this remains a known FP until that representation
# issue is addressed. See doc/python-frontend-architecture.md master inventory
# (section A) and doc/python-frontend-plan.md §7.
d: dict[str, Any] = {"key": 42}
assert d["key"] == 42

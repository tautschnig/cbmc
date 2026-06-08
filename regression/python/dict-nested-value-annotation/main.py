# A dict whose value type is itself a dict (or set) must keep its declared
# type, not fall back to int. Previously dict[str, dict[str, int]] degraded
# to int (the is_safe allowlist omitted dict/set), so a parameter typed
# that way carried no value and assertions over it were vacuously proved
# (a false-proof soundness bug). Regression for github_3647_12_fail.

def f(d: dict[str, dict[str, int]]) -> None:
    for k1, inner in d.items():
        for k2, v in inner.items():
            assert v < 0   # v == 1: genuinely false, must be caught

f({"a": {"b": 1}})

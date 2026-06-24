# On the native SMT-String backend, dict string keys (and python_value
# string values) are boxed behind typed pointers so the byte-imaged dict
# struct stays byte_extract-valid. Before boxing, iterating an untyped
# NESTED dict (whose value is itself a dict) aborted CBMC's unpack_struct
# invariant ("non-constant-width member must come last"). This exercises
# construction, membership, nested subscript, items() iteration, and
# subscript-assign with string keys.


def collect(d: dict) -> int:
    total = 0
    if "p" in d:
        for k, v in d["p"].items():
            total += v
    return total


result = collect({"p": {"a": 1, "b": 2}})
assert result >= 0

# String-keyed dict round-trips: assign, read, membership.
r = {}
r["x"] = 10
r["y"] = 20
assert r["x"] == 10
assert r["y"] == 20
assert "x" in r
assert "z" not in r

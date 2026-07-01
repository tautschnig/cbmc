# PLR §3: dict keys dedup by Python numeric equality (True == 1, hash equal), so
# `{True: 1, 1: 2}` has ONE key with the later value (2). build_dict_value now
# dedups numeric-key constants via python_numeric_key. CPython: len == 1, so this
# `assert len({True: 1, 1: 2}) == 2` raises. Expected: VERIFICATION FAILED.
assert len({True: 1, 1: 2}) == 2

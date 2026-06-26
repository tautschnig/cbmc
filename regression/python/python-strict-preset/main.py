# --python-strict is an opt-in preset that enables the static-strictness family
# (mypy-style): --python-check-annotations, --python-missing-return-check,
# --python-required-kwarg-checks, --python-check-typeddict-fields,
# --python-check-any-arg-attrs and --python-check-iter-none. Here a genuine
# annotation mismatch (int binding assigned a str) must be caught by the preset
# alone, with no individual flag passed. (The preset does NOT change default
# semantics; it just turns the family on.)
n: int = "hello"
assert True

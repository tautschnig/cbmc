# A module-level constant imported via `from MODULE import name` must
# carry its value, not be left undefined (which made assertions over it
# vacuously true — a false proof). Regression for github_2897_2_fail.
from consts import LIMIT, NAME

assert LIMIT == 42
assert NAME == "ok"

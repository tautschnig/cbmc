# PLR §6.2: An assignment statement evaluates the expression list
# and binds the (single) resulting object to the target name. The
# binding is purely by reference — there is no implicit numeric
# coercion at the binding site.
#
# Module-level globals whose RHS the pre-pass can't classify as a
# constant or list literal (math.inf is an Attribute) used to fall
# through to a placeholder int type, which then forced a lossy
# typecast of +inf into a 64-bit signed int. With the fix in place
# the global takes the RHS's actual type on the first assignment.

import math

x = math.inf
assert x > 1e300

y = math.pi
assert 3.14 < y < 3.15

# Regression for exception_div_zero shape: rebinding from int to
# float keeps the cast-to-existing-type behaviour, so a guarded
# assignment that didn't fire still leaves the int value visible.
def safe_div(a: int, b: int) -> float:
    if b == 0:
        raise ZeroDivisionError("by zero")
    return a / b

result = 10
try:
    result = safe_div(5, 0)
except ZeroDivisionError:
    pass
assert result == 10

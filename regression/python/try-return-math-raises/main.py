# Before this fix, if an expression's pending_checks raised an
# exception (e.g. the Option-4 math domain check for
# 'math.sqrt(-1.0)') and the expression sat directly in a 'return'
# statement inside a try body, the return was emitted
# unconditionally — the except handler could not run because the
# function had already returned.
#
# After the fix, the main body of every statement is guarded by
# !__exception_active, so a raise in pending_checks skips the
# 'return' and the handler runs as the PLR requires.

import math


def safe_sqrt_return(x: float) -> float:
    try:
        return math.sqrt(x)
    except ValueError:
        return 0.0


# For any input, the result is non-negative: either sqrt(x) >= 0
# (C library constraint) or 0.0 (except branch).
assert safe_sqrt_return(-1.0) >= 0.0
assert safe_sqrt_return(-0.5) >= 0.0
assert safe_sqrt_return(4.0) >= 0.0

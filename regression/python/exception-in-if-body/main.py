# Differential unsoundness witness §7 (cbmc-py-differential UNSOUNDNESS.md),
# the former cbmc-py KNOWNBUG `exception-propagation-conditional`:
# an implicit exception (ZeroDivisionError here) raised by an
# expression nested inside an `if`/`while`/`for` body must be
# detected. Previously implicit-exception checks were consumed only
# at expression-statement level and were dropped for expressions
# inside compound-statement bodies, so this verified SUCCESSFUL
# despite a real CPython ZeroDivisionError (false negative). Now
# fixed — the divide-by-zero inside the if-body is detected.
x = 1
y = 0
if x == 1:
    z = x // y  # CPython: ZeroDivisionError
assert True

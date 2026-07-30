# Vacuity twin of dict-param-value-read: the read value must be the
# REAL value (CPython: AssertionError on the wrong-constant compare).
def read_get(params=None):
    return params.get("Bucket")


b = read_get(params={"Bucket": "data-service"})
assert b == "auth-service"

# differential: Any / unannotated parameter used unsoundly. An unannotated
# parameter is treated as Any; subscripting it carries no runtime tag
# obligation, so calling first(5) and evaluating 5[0] is accepted. CPython
# raises TypeError ('int' object is not subscriptable). The PLR-correct
# outcome is VERIFICATION FAILED; cbmc currently verifies SUCCESSFUL because
# operations on Any/union values emit no tag check (see plan s0 -- tag
# obligation on Any/union extraction + subscript/attr/operator).
def first(xs):
    return xs[0]

first(5)

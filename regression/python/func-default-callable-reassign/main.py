def mul(x: int, y: int) -> int:
    return x * y


def sub(x: int, y: int) -> int:
    return x - y


cur = mul


def do_op(operand: int, operator=cur) -> int:
    return operator(operand, 2)


# PLR 8.7: `operator`'s default is bound to `cur`'s value at DEF time
# (mul). A later reassignment of `cur` must NOT change the default, so
# both calls compute mul(2, 2) == 4.
assert do_op(2) == 4
cur = sub
assert do_op(2) == 4

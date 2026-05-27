# Phase 4: @icontract.ensure(lambda result: ...) — the
# parameter named `result` resolves to the function's return
# value. The post-process walk inserts an assignment to the
# `result` symbol before each return statement so the
# postcondition assertion can see the return value.
#
# Combined with @require, the body is verified under the
# precondition AND the result satisfies the postcondition.

import icontract


@icontract.require(lambda x: x >= 0)
@icontract.ensure(lambda result: result >= 0)
def double(x: int) -> int:
    return x + x


@icontract.require(lambda x: x > 0)
@icontract.ensure(lambda result: result == x * x)
def square(x: int) -> int:
    return x * x


def main():
    a = double(5)
    assert a == 10

    b = square(3)
    assert b == 9


main()

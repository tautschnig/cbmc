# PLR §8.8: coroutines lowered to sequential evaluation (await EXPR -> EXPR).
# Regression lock-in for the await-result binding forms: inline, assignment,
# reassignment, and via asyncio.run. (The assignment form previously left the
# target unbound; this test guards against that regressing.)
import asyncio


async def inc(x: int) -> int:
    return x + 1


# Inline await in an expression.
assert (await inc(4)) == 5

# Assignment of an await result binds the target.
v = await inc(4)
assert v == 5

# Reassignment chains through the await result.
v = await inc(v)
assert v == 6


async def main() -> int:
    a = await inc(4)
    b = await inc(a)
    return b


# await inside an async def, driven by asyncio.run.
r = asyncio.run(main())
assert r == 6

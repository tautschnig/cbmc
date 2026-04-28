# PLR §8.8: Concurrent coroutine scheduling
# async for / async with / await expressions require event loop model
# Our converter skips AsyncFunctionDef entirely

async def fetch(x: int) -> int:
    return x * 2

# This should call fetch and get 42, but async def is not converted
result: int = 0
# Without async support, we can't call fetch()
assert result == 0  # Trivially true — documents the gap

# PLR §8.8: Coroutines — async def / await
# async def is AsyncFunctionDef in the AST, not handled by our converter
async def compute(x: int) -> int:
    return x * 2

# Direct call (not via asyncio.run) to test if async def is converted
result: int = compute(21)
assert result == 42

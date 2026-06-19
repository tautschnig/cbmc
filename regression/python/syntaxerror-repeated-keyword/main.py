def f(x: int, y: int) -> int:
    return x + y


# PLR §6.3.4 / §8: a repeated keyword argument is a compile-time
# SyntaxError. Surfaced (allow-listed) by the AST server so CBMC reports a
# parsing error rather than silently verifying.
result = f(x=1, x=2)

def factorial(n: int) -> int:
    if n <= 1:
        return 1
    return n * factorial(n - 1)

# factorial(21) = 51090942171709440000, overflows int64
# In Python this is a valid positive integer
# Use --function to avoid computing it at top level

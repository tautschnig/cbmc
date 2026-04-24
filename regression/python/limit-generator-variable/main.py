# Limitation: generator expressions only work with literal iterables
nums = [1, 2, 3, 4, 5]
result = all(x > 0 for x in nums)
assert result

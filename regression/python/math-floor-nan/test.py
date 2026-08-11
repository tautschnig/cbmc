# Regression: math.floor(NaN) must raise ValueError.
import math
x = float("nan")
y = math.floor(x)

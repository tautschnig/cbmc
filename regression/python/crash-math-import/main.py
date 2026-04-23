# Crash: math module function with edge case arguments
import math

x: float = math.acos(1.0)
assert x >= 0.0

import math

# math.radians returns nondet float (no precise model)
# Test verifies the function is resolved without crash
x: float = math.radians(180.0)

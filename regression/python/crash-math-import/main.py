# Test that 'import math; math.func()' style works without crashing
import math

x: float = math.sqrt(4.0)
# sqrt returns nondet float (no precise model), but no crash

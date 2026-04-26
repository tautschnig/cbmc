# PLR §7.11: import statement
# "import math" then "math.sqrt(4)" should work
import math
x: float = math.sqrt(4.0)
assert x > 1.0

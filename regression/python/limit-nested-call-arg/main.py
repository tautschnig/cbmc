# Python Language Reference §6.3.4: calls
# Function call results used as arguments to other calls
def double(x: int) -> int:
    return x * 2

def add(a: int, b: int) -> int:
    return a + b

assert add(double(3), double(4)) == 14

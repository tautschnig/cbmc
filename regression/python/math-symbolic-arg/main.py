import math

def check_trig_identity(x: float) -> None:
    s = math.sin(x)
    c = math.cos(x)
    assert s * s + c * c > 0.99
    assert s * s + c * c < 1.01

check_trig_identity(1.0)

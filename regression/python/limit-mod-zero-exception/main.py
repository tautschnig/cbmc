# PLR §6.7: modulo by zero should raise ZeroDivisionError
try:
    x: int = 2 % 0
    assert False
except ZeroDivisionError:
    pass

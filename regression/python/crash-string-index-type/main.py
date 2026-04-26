# PLR §6.3.2: subscript with wrong type should raise TypeError
numbers = [1, 2, 3]
try:
    x = numbers["invalid"]
    assert False
except TypeError:
    pass

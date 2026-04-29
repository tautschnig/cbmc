def add(**kwargs):
    return kwargs["a"] + kwargs["b"]

result: int = add(a=3, b=4)
assert result == 7

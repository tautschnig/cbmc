def greet(**kwargs):
    name = kwargs["name"]
    return name

result = greet(name="Alice")
assert result == "Alice"

class MyClass:
    value: int = 0

def process(items: list) -> int:
    total: int = 0
    for item in items:
        total = total + item.value
    return total

objs = [MyClass(), MyClass()]
assert process(objs) >= 0

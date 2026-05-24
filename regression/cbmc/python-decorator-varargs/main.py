# PLR §8.7: decorators + *args/Starred-argument unpacking.
#
# A decorator wraps a function in a generic wrapper that takes
# *args. The frontend handles this end-to-end:
#   - Call to f(10) is redirected through wrapper via
#     function_aliases.
#   - The 10 is packed into wrapper's *args list.
#   - Inside wrapper, fn(*args) unpacks the args list back into
#     positional arguments using the target function's arity.
#   - The closure-captured fn binding is resolved through
#     function_aliases to the original function.

def my_dec(fn):
    def wrapper(*args):
        return fn(*args) + 1
    return wrapper


@my_dec
def f(x: int) -> int:
    return x


assert f(10) == 11


# *args forwarding through one level
def add(a: int, b: int) -> int:
    return a + b


def caller(*args):
    return add(*args)


assert caller(2, 3) == 5
assert caller(10, 20) == 30

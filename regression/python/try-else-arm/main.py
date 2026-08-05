# else must NOT run when the try body raised (even though the handler
# clears the exception before the else clause is reached in program order)
log = 0


def f(x: int) -> int:
    global log
    try:
        if x == 1:
            raise ValueError("boom")
        r = 10
    except ValueError:
        r = 20
    else:
        log += 1
        r += 1
    return r


assert f(1) == 20
assert log == 0
assert f(0) == 11
assert log == 1

# raise inside else propagates (not caught by this try's handlers)
hit = 0
try:
    try:
        pass
    except ValueError:
        hit = 99
    else:
        raise ValueError("from-else")
except ValueError:
    hit = 1
assert hit == 1

# try/else/finally ordering
order = []
try:
    order.append(1)
except TypeError:
    order.append(-1)
else:
    order.append(2)
finally:
    order.append(3)
assert order == [1, 2, 3]

box = [0]


def f() -> int:
    raise KeyError()


got = ""
try:
    box[5] = f()        # PLR 6.16: RHS first -> KeyError, not IndexError
except KeyError:
    got = "key"
except IndexError:
    got = "index"
assert got == "key"

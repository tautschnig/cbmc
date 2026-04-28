# PLR §6.2.9: generator with return before yield
def gen():
    return
    yield 1

g = gen()

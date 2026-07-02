# PLR §6.2.9 / PEP 342: gen.close() finalises the generator; a subsequent next()
# raises StopIteration. Modelled by exhausting the consumption cursor.
# CPython: StopIteration; expected: VERIFICATION FAILED.
def g():
    yield 1
    yield 2


it = g()
it.close()
x = next(it)

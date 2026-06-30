# PLR §6.2.9 / PEP 342: gen.send(non-None) on a just-started generator raises
# TypeError ("can't send non-None value to a just-started generator"). The eager
# list-with-cursor model encodes priming in the cursor (0 == not started), and
# the .send() handler raises when the cursor is 0 and the sent value is not None.
# Expected: VERIFICATION FAILED.
def g():
    x = yield 1
    yield x


it = g()
it.send(5)

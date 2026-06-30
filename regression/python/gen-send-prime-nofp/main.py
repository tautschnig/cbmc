# PLR §6.2.9 / PEP 342 no-false-positive guard for generator .send(). None of
# these is a just-started non-None send, so none must raise:
#   - send(None) on a just-started generator primes it (valid);
#   - send(value) after a prior next() resumes (valid);
# and the whole-group yield-count fix means a `yield`-expression generator is
# counted correctly (two yields), so neither resume runs off the end.
# Expected: VERIFICATION SUCCESSFUL.
def g():
    x = yield 1
    yield x


# send(None) before start: valid (equivalent to next()).
it1 = g()
it1.send(None)

# send(value) after priming with next(): valid.
it2 = g()
a = next(it2)
it2.send(7)
assert a == 1

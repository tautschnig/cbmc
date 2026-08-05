# twin: wrong expectation must FAIL (else DID run)
try:
    r = 1
except ValueError:
    r = 2
else:
    r = 3
assert r == 1

# chr(200000) is not empty: this assertion fails.
k = nondet_int()
if k == 200000:
    assert len(chr(k)) == 0

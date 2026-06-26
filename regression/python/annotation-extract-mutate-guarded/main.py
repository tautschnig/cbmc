# Extraction-then-mutate soundness with an ANNOTATED extraction. `r: list = c[0]`
# extracts a mutable element; `r.append(99)` mutates it in place, so in CPython
# c[0] becomes [1,2,99] and the assert is false. The guard (havoc the source
# container on in-place mutation of an extracted alias) must fire on the
# annotated-assignment path too (it previously only fired for `r = c[0]`).
def main():
    c: list = [[1, 2], [3, 4]]
    r: list = c[0]
    r.append(99)
    assert c[0] == [1, 2]   # FALSE in CPython; must not verify
main()

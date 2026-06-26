# KNOWNBUG (false proof): the extraction-then-mutate guard (r = c[i]; r.append())
# havocs the source container and is sound -- but it is BYPASSED when the
# extracted variable carries an annotation (`r: list = c[0]`). The AnnAssign path
# does not record the extracted_container_alias, so the mutation is invisible and
# cbmc proves the stale value. CPython: c[0] becomes [1,2,99] -> the assert fails.
def main():
    c: list = [[1, 2], [3, 4]]
    r: list = c[0]
    r.append(99)
    assert c[0] == [1, 2]   # FALSE in CPython (c[0] is now [1,2,99])
main()

# PLR 6.10.1 user-__eq__ SET display: a set IS a dict with unit
# values -- the user-eq dict constructor's statement-level __eq__
# scans give FIRST-occurrence-wins dedup, exactly the set rule
# (perf-study k5, set edition).
class K:
    def __init__(s, n, tag):
        s.n, s.tag = n, tag

    def __hash__(s):
        return hash(s.n)

    def __eq__(s, o):
        return isinstance(o, K) and s.n == o.n


s = {K(1, 'first'), K(2, 'x'), K(1, 'second')}
assert len(s) == 2

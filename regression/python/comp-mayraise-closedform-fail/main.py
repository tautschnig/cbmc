# twin: ineligible comprehensions terminate LOUDLY (model-bound /
# truncation), never hang. A filtered comprehension over a symbolic
# length keeps the bounded loop with a fail-closed guard.
def fetch() -> list[int]: ...


xs = fetch()
ys = [x for x in xs if x > 0]
assert len(ys) <= len(xs)

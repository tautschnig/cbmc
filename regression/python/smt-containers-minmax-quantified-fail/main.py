# twin: uncaught empty-sequence ValueError on a reachable path FAILS
def fetch() -> list[int]: ...


xs = fetch()
if len(xs) == 0:
    m = max(xs)

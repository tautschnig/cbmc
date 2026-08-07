# twin: the un-deduped size must NOT verify.
src = [1, 2, 1]
d = {k: k * 10 for k in src}
assert len(d) == 3

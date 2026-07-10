# PLR §6.10.1: list == compares element-by-element up to length, NOT the whole
# fixed-size backing array. list(enumerate(xs)) leaves out-of-bounds reads of xs
# beyond `length`, whereas the literal pads with zeros; a plain struct equality
# would wrongly report them unequal, letting `!=` be FALSE-PROVED (mutation-
# oracle). String elements compare by content (github_3127_2 regression guard).
xs = [7, 8]
assert list(enumerate(xs)) == [(0, 7), (1, 8)]
assert list(zip([1, 2, 3], [9])) == [(1, 9)]
assert list(enumerate(xs)) != [(0, 7)]
ys = []
ys.append("a")
ys.append("b")
assert ys == ["a", "b"]

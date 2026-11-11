# Equal set elements are dropped (PLR 6.2.6): strings computed at run time.
x = input()
y = x + "b"
if y == "ab":
    assert len(set([y, "ab"])) == 1

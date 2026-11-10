def check(x: float) -> None:
    if str(x) == "2.75":
        assert x == 2.75
    if x > 0:
        assert str(x)[0] != "-"
    if x < 0:
        assert str(x)[0] == "-"

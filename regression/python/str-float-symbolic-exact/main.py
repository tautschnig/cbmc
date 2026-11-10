def check(x: float) -> None:
    if x == -1.5:
        assert str(x) == "-1.5"
    if x == 0.1:
        assert str(x) == "0.1"
    if x == 1e16:
        assert str(x) == "1e+16"
    if x == 1.0 / 3.0:
        assert str(x) == "0.3333333333333333"

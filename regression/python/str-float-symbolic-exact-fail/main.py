def check(x: float) -> None:
    if x == 1.0 / 3.0:
        assert str(x) != "0.3333333333333333"

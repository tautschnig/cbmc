def check(s: str, k: int) -> None:
    if s == "abc":
        if k == 10:
            assert s.find("", k) == -1
            assert s.find("b", k) == -1
        if k == -1:
            assert s.find("c", k) == 2
            assert s.find("a", k) == -1
        if k == -10:
            assert s.find("a", k) == 0
        if k == 2:
            assert s.find("", k) == 2
        if k == 3:
            assert s.find("", k) == 3

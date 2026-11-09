def check(s: str) -> None:
    if s == "abc":
        assert s.upper() == "ABC"
    if s == "Hi":
        assert s.lower() == "hi"

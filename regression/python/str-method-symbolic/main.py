def check(s: str) -> None:
    t: str = s.strip()
    assert len(t) <= len(s)

check("  hello  ")

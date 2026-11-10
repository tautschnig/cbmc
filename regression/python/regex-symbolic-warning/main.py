import re
def check(s: str) -> None:
    if re.fullmatch("a+", s):
        assert len(s) > 0

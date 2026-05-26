def validate(s: str):
    last = s[len(s)-1]
    assert not last.isdigit()

validate("Livro")

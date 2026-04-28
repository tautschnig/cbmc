# PLib stdtypes: Unicode string operations
text: str = "Café"
assert text.isalpha()  # Fails: non-ASCII chars not in [a-zA-Z]

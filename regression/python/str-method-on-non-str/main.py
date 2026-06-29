# PLR §4: a str-only method (upper/lower/strip/...) on a concrete non-str
# built-in receiver raises AttributeError ('int' object has no attribute
# 'upper'). Previously silently accepted.
def main() -> None:
    n = 5
    n.upper()   # AttributeError: 'int' object has no attribute 'upper'


main()

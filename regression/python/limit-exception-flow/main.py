def safe_div(a: int, b: int) -> int:
    try:
        return a // b
    except ZeroDivisionError:
        return 0

assert safe_div(10, 0) == 0

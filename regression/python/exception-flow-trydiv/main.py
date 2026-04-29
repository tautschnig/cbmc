def safe_div(a: int, b: int) -> int:
    result: int = 0
    try:
        result = a // b
    except ZeroDivisionError:
        result = 0
    return result

assert safe_div(10, 0) == 0

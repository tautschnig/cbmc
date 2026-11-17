# Characters picked from a constant string by a computed index, prepended in
# a loop.
def to_base(m: int, base: int) -> str:
    alphabet = "0123456789abcdef"
    out = ""
    while True:
        out = alphabet[m % base] + out
        m = m // base
        if m == 0:
            break
    return out


assert to_base(3054, 16) == "bee"
assert to_base(5, 2) == "110"

def nibble(word: int) -> int:
    return (word >> 4) & 0xf


def collect(words: list) -> list:
    out = []
    for w in words:
        out.append(nibble(w))
    return out


r = collect([0x31, 0x45])
assert r == [3, 4]
assert r[0] + r[1] == 7

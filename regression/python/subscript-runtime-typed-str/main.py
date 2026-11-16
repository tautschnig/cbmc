def pick(t: dict, k: str) -> bool:
    w = t[k][0]
    return w[1] == 'b' and w[2:4] == 'cd' and int(w[4:6], 16) == 0xef


assert pick({'s': ['abcdef']}, 's')

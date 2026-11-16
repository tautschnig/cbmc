def pick(t: dict, k: str) -> str:
    return t[k][0][7]


pick({'s': ['abc']}, 's')     # IndexError: string index out of range

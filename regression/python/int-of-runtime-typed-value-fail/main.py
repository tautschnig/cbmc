def get(t: dict, k: str) -> int:
    return int(t[k][0])


row = {'s': ['42'], 'i': [7], 'b': [True], 'f': [2.9]}
assert get(row, 's') == 42
assert get(row, 'i') == 7
assert get(row, 'b') == 1
assert get(row, "f") == 3

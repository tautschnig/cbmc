class Table:
    def __init__(self, cols: dict) -> None:
        self.cols = cols

    def __getitem__(self, key: str) -> list:
        return self.cols[key]

    def __len__(self) -> int:
        return len(self.cols['id'])


def rows(t) -> int:
    n = 0
    for i in range(len(t)):
        if t['id'][i] > 0:
            n += 1
    return n


t = Table({'id': [3, 0, 5]})
assert len(t) == 3
assert t['id'][2] == 5
assert rows(t) == 3

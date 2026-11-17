class Log:
    def __init__(self) -> None:
        self.hits = 0
        self.rows = ['abcdef']

    def __getitem__(self, k: str):
        self.hits = self.hits + 1
        return self.rows


def use(t) -> int:
    s = t['r'][0][1:3]
    assert s == 'bc'
    return t.hits


assert use(Log()) == 1

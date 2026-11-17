class Table:
    def __init__(self, names: list[str], sizes: list[int]) -> None:
        self.names = names
        self.sizes = sizes

    def __len__(self) -> int:
        return len(self.names)

    def __getitem__(self, k: str):
        if k == 'name':
            return self.names
        return self.sizes


def total(t) -> int:
    s = 0
    for i in range(len(t)):
        if t['name'][i] == 'B':
            s = s + t['size'][i]
    return s


assert total(Table(['A', 'B', 'B'], [1, 2, 3])) == 6

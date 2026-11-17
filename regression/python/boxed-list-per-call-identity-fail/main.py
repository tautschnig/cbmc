class Table:
    def __init__(self, names: list[str], sizes: list[int]) -> None:
        self.names = names
        self.sizes = sizes

    def __getitem__(self, k: str):
        if k == 'name':
            return self.names
        return self.sizes


t1 = Table(['A', 'B'], [1, 2])
t2 = Table(['C', 'D'], [3, 4])
a = t1['name']
b = t2['name']
assert a[0] == 'C' and b[0] == 'C'

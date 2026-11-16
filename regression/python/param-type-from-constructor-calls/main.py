class Frame:
    def __init__(self, a: list) -> None:
        self.a = a

    def __len__(self) -> int:
        return len(self.a)

    def __getitem__(self, k: str) -> list:
        return self.a


def count(x) -> int:
    n = 0
    for i in range(len(x)):
        if x['k'][i] == 'ON':
            n += 1
    return n


assert count(Frame(['ON', 'OFF', 'ON'])) == 2

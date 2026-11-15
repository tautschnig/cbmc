SLOTS = 3
LO = 0x40
HI = LO + 0x10 - 1
bins = [[] for _ in range(SLOTS)]
log = []
log.append(1)
if __name__ == "__main__":
    log.append(99)


def put(k: int, v: int) -> None:
    bins[k].append(v)


def count(k: int) -> int:
    return len(bins[k])


def inside(v: int) -> bool:
    return LO <= v <= HI


def log_size() -> int:
    return len(log)

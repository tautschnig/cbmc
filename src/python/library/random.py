"""
Verification model of the `random` module.

All random functions return conservative defaults (the low end
of their range) — a sound over-approximation since the caller
already reasons about bounds, not specific values.
"""


def random() -> float:
    return 0.0


def uniform(a: float, b: float) -> float:
    return a


def triangular(low: float = 0.0, high: float = 1.0, mode: float = 0.5) -> float:
    return low


def randrange(start: int, stop: int = 0, step: int = 1) -> int:
    return start


def randint(a: int, b: int) -> int:
    # PLR / random module §6.3: randint returns a nondet int N
    # with a <= N <= b. The previous under-approximation `return a`
    # missed verification paths involving any intermediate value
    # of the range (e.g. `if random.randint(0, 1): ...` was always
    # entering the false branch).
    n: int = nondet_int()
    __ESBMC_assume(n >= a and n <= b)
    return n


def getrandbits(k: int) -> int:
    return 0


def choice(seq):
    return seq[0] if hasattr(seq, '__getitem__') else None


def choices(population, weights=None, *, cum_weights=None, k: int = 1):
    return [population[0]] if population else []


def sample(population, k: int, *, counts=None):
    return list(population)[:k]


def shuffle(x, random=None):
    return None


def gauss(mu: float = 0.0, sigma: float = 1.0) -> float:
    return mu


def normalvariate(mu: float = 0.0, sigma: float = 1.0) -> float:
    return mu


def lognormvariate(mu: float, sigma: float) -> float:
    return mu


def expovariate(lambd: float = 1.0) -> float:
    return 1.0


def vonmisesvariate(mu: float, kappa: float) -> float:
    return mu


def gammavariate(alpha: float, beta: float) -> float:
    return alpha


def betavariate(alpha: float, beta: float) -> float:
    return 0.5


def paretovariate(alpha: float) -> float:
    return 1.0


def weibullvariate(alpha: float, beta: float) -> float:
    return alpha


def seed(a=None, version: int = 2):
    return None


def getstate():
    return (3, (0,), None)


def setstate(state):
    return None


class Random:
    def __init__(self, x=None):
        pass

    def seed(self, a=None, version: int = 2):
        return None

    def random(self) -> float:
        return random()

    def uniform(self, a: float, b: float) -> float:
        return uniform(a, b)

    def randint(self, a: int, b: int) -> int:
        return randint(a, b)

    def randrange(self, start: int, stop: int = 0, step: int = 1) -> int:
        return randrange(start, stop, step)

    def choice(self, seq):
        return choice(seq)

    def shuffle(self, x):
        return shuffle(x)

    def sample(self, population, k: int):
        return sample(population, k)

    def gauss(self, mu: float = 0.0, sigma: float = 1.0) -> float:
        return mu

    def getstate(self):
        return getstate()

    def setstate(self, state):
        return None


class SystemRandom(Random):
    pass
